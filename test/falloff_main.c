int simple(void);
int both(int c);
int via_switch(int x);
int via_enum(int c);
int after_loop(void);
void void_ok(void);

int
main(void)
{
	if(simple() != 42)
		return 1;
	if(both(1) != 1 || both(0) != 0)
		return 2;
	if(via_switch(0) != 10 || via_switch(1) != 20 || via_switch(2) != 30)
		return 3;
	if(via_enum(0) != 1 || via_enum(1) != 2 || via_enum(2) != 3)
		return 5;
	if(after_loop() != 3)
		return 4;
	void_ok();
	return 0;
}
