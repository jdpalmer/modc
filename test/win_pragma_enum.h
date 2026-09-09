/* __pragma before typedef enum must not cause enumerator redefinition in prescan. */
__pragma(pack(push, 8))
typedef enum _PackEnum {
	PackEnumA,
	PackEnumB
} PackEnum;
__pragma(pack(pop))
