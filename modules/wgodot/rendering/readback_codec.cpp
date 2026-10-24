// wgodot-changes::file
#include "readback_codec.h"
#ifdef TOOLS_ENABLED
#include "core/object/worker_thread_pool.h"
#include "core/os/memory.h"
#include <zstd.h>
#if defined(__SSE2__) || defined(_M_X64)
#include <emmintrin.h>
#endif

namespace WReadbackCodec {
namespace {
struct Context {
	ZSTD_CCtx *zstd = ZSTD_createCCtx();
	uint8_t *scratch = static_cast<uint8_t *>(memalloc(BLOCK_BYTES));
	~Context() {
		ZSTD_freeCCtx(zstd);
		memfree(scratch);
	}
};

void shuffle(const uint8_t *p_source, uint8_t *p_target, int p_bytes) {
	const int count = p_bytes / 8;
	int item = 0;
#if defined(__SSE2__) || defined(_M_X64)
	// Transpose eight 8-byte elements at a time. SSE2 is baseline on x86_64;
	// this also avoids a per-byte interpreted/debug-build inner loop.
	for (; item + 8 <= count; item += 8) {
		const uint8_t *src = p_source + item * 8;
		__m128i a = _mm_loadl_epi64(reinterpret_cast<const __m128i *>(src));
		__m128i b = _mm_loadl_epi64(reinterpret_cast<const __m128i *>(src + 8));
		__m128i c = _mm_loadl_epi64(reinterpret_cast<const __m128i *>(src + 16));
		__m128i d = _mm_loadl_epi64(reinterpret_cast<const __m128i *>(src + 24));
		__m128i e = _mm_loadl_epi64(reinterpret_cast<const __m128i *>(src + 32));
		__m128i f = _mm_loadl_epi64(reinterpret_cast<const __m128i *>(src + 40));
		__m128i g = _mm_loadl_epi64(reinterpret_cast<const __m128i *>(src + 48));
		__m128i h = _mm_loadl_epi64(reinterpret_cast<const __m128i *>(src + 56));
		__m128i ab = _mm_unpacklo_epi8(a, b);
		__m128i cd = _mm_unpacklo_epi8(c, d);
		__m128i ef = _mm_unpacklo_epi8(e, f);
		__m128i gh = _mm_unpacklo_epi8(g, h);
		__m128i abcd0 = _mm_unpacklo_epi16(ab, cd);
		__m128i abcd1 = _mm_unpackhi_epi16(ab, cd);
		__m128i efgh0 = _mm_unpacklo_epi16(ef, gh);
		__m128i efgh1 = _mm_unpackhi_epi16(ef, gh);
		__m128i columns[4] = {
			_mm_unpacklo_epi32(abcd0, efgh0), _mm_unpackhi_epi32(abcd0, efgh0),
			_mm_unpacklo_epi32(abcd1, efgh1), _mm_unpackhi_epi32(abcd1, efgh1)
		};
		for (int pair = 0; pair < 4; pair++) {
			_mm_storel_epi64(reinterpret_cast<__m128i *>(p_target + (pair * 2) * count + item), columns[pair]);
			_mm_storel_epi64(reinterpret_cast<__m128i *>(p_target + (pair * 2 + 1) * count + item), _mm_srli_si128(columns[pair], 8));
		}
	}
#endif
	for (int byte = 0; byte < 8; byte++) {
		for (int i = item; i < count; i++) {
			p_target[byte * count + i] = p_source[i * 8 + byte];
		}
	}
}

struct Block {
	const uint8_t *source = nullptr;
	int bytes = 0;
	bool shuffled = false;
	PackedByteArray compressed;
	Error error = OK;
};

void encode_block(void *p_userdata, uint32_t p_index) {
	Block &block = static_cast<Block *>(p_userdata)[p_index];
	thread_local Context context;
	if (!context.zstd || !context.scratch) {
		block.error = ERR_OUT_OF_MEMORY;
		return;
	}
	const uint8_t *source = block.source;
	if (block.shuffled) {
		shuffle(source, context.scratch, block.bytes);
		source = context.scratch;
	}
	block.compressed.resize(ZSTD_compressBound(block.bytes));
	// Byte planes compress better than interleaved float components even with
	// the fast strategy. Byte/texture inputs retain normal lossless level 1.
	size_t size = ZSTD_compressCCtx(context.zstd, block.compressed.ptrw(), block.compressed.size(), source, block.bytes, block.shuffled ? -1 : 1);
	if (ZSTD_isError(size)) {
		block.error = ERR_CANT_CREATE;
		block.compressed.clear();
		return;
	}
	block.compressed.resize(size);
}
} // namespace

Vector<Output> encode(const Vector<Input> &p_inputs) {
	Vector<Block> blocks;
	for (const Input &input : p_inputs) {
		ERR_FAIL_COND_V(input.shuffle_stride != 0 && (input.shuffle_stride != 8 || input.bytes.size() % 8), Vector<Output>());
		for (int64_t offset = 0; offset < input.bytes.size(); offset += BLOCK_BYTES) {
			Block block;
			block.source = input.bytes.ptr() + offset;
			block.bytes = MIN(int64_t(BLOCK_BYTES), input.bytes.size() - offset);
			block.shuffled = input.shuffle_stride != 0;
			blocks.push_back(block);
		}
	}
	if (!blocks.is_empty()) {
		// One flat group over all planes: a large changing plane cannot leave
		// most cores idle after the smaller/reused planes have finished.
		auto task = WorkerThreadPool::get_singleton()->add_native_group_task(
				&encode_block, blocks.ptrw(), blocks.size(), -1, true, "Encode capture blocks");
		WorkerThreadPool::get_singleton()->wait_for_group_task_completion(task);
	}
	Vector<Output> outputs;
	int first = 0;
	for (const Input &input : p_inputs) {
		const int count = (input.bytes.size() + BLOCK_BYTES - 1) / BLOCK_BYTES;
		Output output;
		int64_t size = 0;
		for (int i = first; i < first + count; i++) {
			size += blocks[i].compressed.size();
			if (blocks[i].error != OK) {
				output.error = blocks[i].error;
			}
		}
		if (output.error == OK) {
			output.bytes.resize(size);
			uint8_t *destination = output.bytes.ptrw();
			for (int i = first; i < first + count; i++) {
				memcpy(destination, blocks[i].compressed.ptr(), blocks[i].compressed.size());
				destination += blocks[i].compressed.size();
			}
		}
		outputs.push_back(output);
		first += count;
	}
	return outputs;
}
} // namespace WReadbackCodec
#endif
