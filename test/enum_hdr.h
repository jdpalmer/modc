/* Headers keep C enum/int implicits. */

enum HdrColor {
	HDR_RED,
	HDR_GREEN
};

int
hdr_from_int(int i)
{
	HdrColor c;

	c = i;
	return c;
}
