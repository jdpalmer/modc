/* Host cc harness: do not include attr_hdr.h (__declspec is for modc headers). */
int use_attrs(void);

int
main(void)
{
	return use_attrs() == 1 + 3 + 9 + 14 ? 0 : 1;
}
