/* Headers may still use C implicit fallthrough. */

int
hdr_fall(int x)
{
	int s;

	s = 0;
	switch(x) {
	case 1:
		s = s + 1;
	case 2:
		s = s + 2;
		break;
	}
	return s;
}
