// wgodot-changes::file
using System;
using System.IO;
using System.IO.Compression;
using System.Security.Cryptography;

public static class WGodotBrotli
{
    public static long Compress(string path)
    {
        byte[] input = File.ReadAllBytes(path);
        byte[] output = new byte[BrotliEncoder.GetMaxCompressedLength(input.Length)];
        // Maximum standard Brotli quality and window; large-window Brotli is not HTTP compatible.
        if (!BrotliEncoder.TryCompress(input, output, out int written, quality: 11, window: 24))
        {
            throw new InvalidOperationException("Brotli compression failed: " + path);
        }

        string destination = path + ".br";
        using (FileStream stream = File.Create(destination))
        {
            stream.Write(output, 0, written);
        }

        using (FileStream stream = File.OpenRead(destination))
        using (var decoded = new BrotliStream(stream, CompressionMode.Decompress))
        using (SHA256 hash = SHA256.Create())
        {
            if (!CryptographicOperations.FixedTimeEquals(hash.ComputeHash(input), hash.ComputeHash(decoded)))
            {
                throw new InvalidDataException("Brotli verification failed: " + path);
            }
        }
        return written;
    }
}
