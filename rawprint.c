
#include <stdio.h>
#include <stdint.h>
#include <ctype.h>

int rawprint(FILE *output, const int indent, const void *data, const size_t size, const size_t byte_per_word, const size_t word_per_line)
{
	const uint8_t *pload = data;
	const uint8_t *from = pload;
	size_t idx;
	int res = 0;
	int need_pfx = 1;

	for (idx = 0 ; idx < size ; idx ++) {
		int ascii_dump = 0;

		if (need_pfx) {
			res += fprintf(output, "%*s", indent, "");
			need_pfx = 0;
		}

		res += fprintf(output, "%02x%s", pload[idx], (idx > 1 && ((idx + 1)% byte_per_word) == 0) ? "  " : " ");

		if (idx + 1 >= size) {
			size_t done;

			done = idx + 1;

			do {
				const size_t rem = byte_per_word - (done % byte_per_word);

				if (rem != 0 && (done % (byte_per_word * word_per_line)) != 0) {
					for (size_t i = 0 ; i < rem ; i++)
						res += fprintf(output, "%s", "   ");
					res += fprintf(output, " ");
					done += rem;

				}

			} while ((done % (byte_per_word * word_per_line)) != 0);

			ascii_dump = 1;

		} else if (idx > 0 && ((idx + 1) % (byte_per_word * word_per_line)) == 0)
			ascii_dump = 1;

		if (ascii_dump) {
			for (const uint8_t *ptr = from ; ptr <= pload + idx ; ptr ++)
				res += fprintf(output, "%c", isprint(*ptr) ? *ptr : '.');
			from = pload + idx + 1;
			res += fprintf(output, "\n");
			need_pfx = 1;
		}
	}

	return res;
}
