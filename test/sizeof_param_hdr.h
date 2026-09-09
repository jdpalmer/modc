/* Headers may sizeof an array parameter (yields pointer size). */

int
hdr_sizeof_param(int a[4])
{
	return (int)sizeof(a);
}
