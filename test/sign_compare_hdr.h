/* Headers may mix signed and unsigned in comparisons. */

int
hdr_sign_compare(int i, unsigned u)
{
	return i < u;
}
