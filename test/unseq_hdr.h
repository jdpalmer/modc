/* Headers may keep C-style i = i++ (undefined in C; not diagnosed here). */

static int
hdr_unseq(int i)
{
	i = i++;
	return i;
}
