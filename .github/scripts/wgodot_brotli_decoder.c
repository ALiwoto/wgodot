// wgodot-changes::file
#include <brotli/decode.h>
#include <stdlib.h>

enum { BUFFER_SIZE = 65536 };

typedef struct {
	BrotliDecoderState *decoder;
	uint8_t input[BUFFER_SIZE];
	uint8_t output[BUFFER_SIZE];
	const uint8_t *next_input;
	size_t remaining_input;
	size_t output_size;
} WGodotBrotliStream;

WGodotBrotliStream *wg_brotli_create(void) {
	WGodotBrotliStream *stream = calloc(1, sizeof(WGodotBrotliStream));
	if (!stream) {
		return NULL;
	}
	stream->decoder = BrotliDecoderCreateInstance(NULL, NULL, NULL);
	if (!stream->decoder) {
		free(stream);
		return NULL;
	}
	return stream;
}

void wg_brotli_destroy(WGodotBrotliStream *stream) {
	BrotliDecoderDestroyInstance(stream->decoder);
	free(stream);
}

uint8_t *wg_brotli_input(WGodotBrotliStream *stream) {
	return stream->input;
}

uint8_t *wg_brotli_output(WGodotBrotliStream *stream) {
	return stream->output;
}

size_t wg_brotli_output_size(WGodotBrotliStream *stream) {
	return stream->output_size;
}

int wg_brotli_decode(WGodotBrotliStream *stream, size_t input_size) {
	if (input_size) {
		if (stream->remaining_input || input_size > BUFFER_SIZE) {
			return BROTLI_DECODER_RESULT_ERROR;
		}
		stream->next_input = stream->input;
		stream->remaining_input = input_size;
	}
	uint8_t *next_output = stream->output;
	size_t available_output = BUFFER_SIZE;
	BrotliDecoderResult result = BrotliDecoderDecompressStream(stream->decoder,
			&stream->remaining_input, &stream->next_input, &available_output, &next_output, NULL);
	stream->output_size = BUFFER_SIZE - available_output;
	if (result == BROTLI_DECODER_RESULT_SUCCESS && stream->remaining_input) {
		return BROTLI_DECODER_RESULT_ERROR;
	}
	return result;
}
