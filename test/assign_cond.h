/* Headers may use C-style assignment in conditions. */

int
hdr_assign_cond(int x)
{
	if(x = 0)
		return 1;
	return 0;
}
