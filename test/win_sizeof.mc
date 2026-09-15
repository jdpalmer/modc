#include <windows.h>
#ifdef WOW64_FLOATING_SAVE_AREA
/* type exists after include - won't compile as #ifdef on typedef */
#endif
int main(void) {
	return sizeof(DWORD);
}
