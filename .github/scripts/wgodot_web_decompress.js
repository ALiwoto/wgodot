// wgodot-changes::file
// WGB1: four magic bytes, uint32 little-endian decoded size, then Brotli data.
// Ordinary files have no wrapper and pass through without decompression.
(function () {
	const decoderURL = new URL(document.currentScript.src.replace(/\.decompress\.js$/, '.brotli-decoder.js'));
	let decoderPromise;
	let activeDecoders = 0;

	function streamFrom(iterator) {
		return new ReadableStream({
			async pull(controller) {
				try {
					const result = await iterator.next();
					if (result.done) {
						controller.close();
					} else {
						controller.enqueue(result.value);
					}
				} catch (error) {
					controller.error(error);
				}
			},
			async cancel() {
				await iterator.return();
			},
		});
	}

	async function* remainingBytes(reader, prefix, skip) {
		try {
			for (const chunk of prefix) {
				if (skip >= chunk.length) {
					skip -= chunk.length;
				} else {
					yield chunk.subarray(skip);
					skip = 0;
				}
			}
			while (true) {
				const result = await reader.read();
				if (result.done) {
					return;
				}
				yield result.value;
			}
		} finally {
			await reader.cancel();
			reader.releaseLock();
		}
	}

	async function* decodeFallback(source) {
		// Compile the existing thirdparty/brotli decoder once; each stream owns its state.
		const reader = source.getReader();
		let module;
		let state;
		let status = 2; // NEEDS_MORE_INPUT
		activeDecoders++;
		try {
			if (!decoderPromise) {
				decoderPromise = import(decoderURL.href).then((factory) => factory.default());
			}
			module = await decoderPromise;
			state = module._wg_brotli_create();
			if (!state) {
				throw new Error('Cannot allocate Brotli decoder.');
			}
			while (true) {
				const { value, done } = await reader.read();
				if (done) {
					if (status !== 1) {
						throw new Error('Truncated Brotli asset.');
					}
					return;
				}
				for (let offset = 0; offset < value.length;) {
					if (status === 1) {
						throw new Error('Unexpected bytes after Brotli asset.');
					}
					const size = Math.min(65536, value.length - offset);
					module.HEAPU8.set(value.subarray(offset, offset + size), module._wg_brotli_input(state));
					offset += size;
					let inputSize = size;
					do {
						status = module._wg_brotli_decode(state, inputSize);
						inputSize = 0;
						if (status === 0) {
							throw new Error('Invalid Brotli asset.');
						}
						const outputSize = module._wg_brotli_output_size(state);
						if (outputSize) {
							const output = module._wg_brotli_output(state);
							// Copy before another decoder call can reuse or grow WASM memory.
							yield module.HEAPU8.slice(output, output + outputSize);
						}
					} while (status === 3); // NEEDS_MORE_OUTPUT
				}
			}
		} finally {
			if (state) {
				module._wg_brotli_destroy(state);
			}
			if (--activeDecoders === 0) {
				// Let the browser reclaim the decoder's WASM memory after loading.
				decoderPromise = null;
			}
			await reader.cancel();
			reader.releaseLock();
		}
	}

	async function* checkSize(source, expected) {
		const reader = source.getReader();
		let size = 0;
		try {
			while (true) {
				const { value, done } = await reader.read();
				if (done) {
					if (size !== expected) {
						throw new Error(`Incomplete asset: expected ${expected} bytes, received ${size}.`);
					}
					return;
				}
				size += value.length;
				if (size > expected) {
					throw new Error('Decompressed asset exceeds its declared size.');
				}
				yield value;
			}
		} finally {
			await reader.cancel();
			reader.releaseLock();
		}
	}

	async function decodeResponse(response) {
		if (!response.body) {
			return response;
		}
		const reader = response.body.getReader();
		const prefix = [];
		const header = new Uint8Array(8);
		let length = 0;
		try {
			while (length < header.length) {
				const { value, done } = await reader.read();
				if (done) {
					break;
				}
				prefix.push(value);
				const count = Math.min(value.length, header.length - length);
				header.set(value.subarray(0, count), length);
				length += count;
			}
		} catch (error) {
			await reader.cancel();
			reader.releaseLock();
			throw error;
		}
		const compressed = length >= 4 && header[0] === 87 && header[1] === 71 && header[2] === 66 && header[3] === 49;
		if (compressed && length < 8) {
			await reader.cancel();
			reader.releaseLock();
			throw new Error('Truncated WGB1 asset header.');
		}
		let body = streamFrom(remainingBytes(reader, prefix, compressed ? 8 : 0));
		const headers = new Headers(response.headers);
		if (compressed) {
			let nativeDecoder;
			try {
				nativeDecoder = new DecompressionStream('brotli');
			} catch (_) {
				// HTTP Brotli support does not imply JavaScript Brotli support.
			}
			body = nativeDecoder ? body.pipeThrough(nativeDecoder) : streamFrom(decodeFallback(body));
			const expected = new DataView(header.buffer).getUint32(4, true);
			body = streamFrom(checkSize(body, expected));
			headers.delete('Content-Encoding');
			headers.set('Content-Length', String(expected));
		}
		return new Response(body, { status: response.status, statusText: response.statusText, headers });
	}

	window.WGodotDecompress = decodeResponse;
}());
