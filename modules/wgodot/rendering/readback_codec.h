// wgodot-changes::file
#pragma once
#ifdef TOOLS_ENABLED
#include "core/error/error_list.h"
#include "core/variant/variant.h"
class WorkerThreadPool;

// Capture-local lossless storage. Blocks are independent standard Zstd frames;
// optional byte-plane shuffling is reversed after decompression, without a
// dependency on any other image/frame. No change to project resource codecs.
namespace WReadbackCodec {
constexpr int BLOCK_BYTES = 256 * 1024;
struct Input {
	PackedByteArray bytes;
	int shuffle_stride = 0;
	int compression_level = 1;
};
struct Output {
	PackedByteArray bytes;
	Error error = OK;
};
Vector<Output> encode(const Vector<Input> &p_inputs, WorkerThreadPool *p_pool = nullptr);
}
#endif
