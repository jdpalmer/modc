int via_call(void);
int via_assign(void);
int via_nested(void);
int named_no_upcast_compiles_field(void);
int via_value_call(void);
int via_value_assign(void);
int via_value_nested(void);
int via_ptr_value(void);

int
main(void)
{
	if(!via_call())
		return 1;
	if(!via_assign())
		return 2;
	if(!via_nested())
		return 3;
	if(!named_no_upcast_compiles_field())
		return 4;
	if(!via_value_call())
		return 5;
	if(!via_value_assign())
		return 6;
	if(!via_value_nested())
		return 7;
	if(!via_ptr_value())
		return 8;
	return 0;
}
