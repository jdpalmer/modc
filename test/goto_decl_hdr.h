/* Headers may goto over a declaration. */

int
hdr_goto_decl(void)
{
	goto skip;
	int x = 1;
skip:
	return x;
}
