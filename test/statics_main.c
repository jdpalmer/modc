int bump(void);
int bump_from(void);
int static_arr_sum(void);
int arr_at(int);
int sparse_at(int);
int gpx(void);
int gpy(void);
int msg_at(int);
int designated_global_ok(void);
int designated_local_ok(void);
int designated_inferred_ok(void);
int field_cursor_ok(void);

int
main(void)
{
	if(bump() != 1)
		return 1;
	if(bump() != 2)
		return 2;
	if(bump_from() != 6)
		return 3;
	if(bump_from() != 7)
		return 4;
	if(static_arr_sum() != 15)
		return 5;
	if(arr_at(0) != 1 || arr_at(1) != 2 || arr_at(2) != 3)
		return 6;
	if(sparse_at(0) != 1 || sparse_at(1) != 0 || sparse_at(2) != 3)
		return 7;
	if(gpx() != 10 || gpy() != 20)
		return 8;
	if(msg_at(0) != 'h' || msg_at(1) != 'i' || msg_at(2) != 0)
		return 9;
	if(!designated_global_ok())
		return 10;
	if(!designated_local_ok())
		return 11;
	if(!designated_inferred_ok())
		return 12;
	if(!field_cursor_ok())
		return 13;
	return 0;
}
