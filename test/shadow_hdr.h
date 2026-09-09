/* Headers may shadow; user TUs may not. */
static inline int
shadow_ok(int x)
{
	{
		int x;

		x = 1;
		return x;
	}
}
