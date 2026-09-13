static char lower(char c){
if(c>='A'&&c<='Z'){
return(char)(c+('a'-'A'));
}
return c;
}

char[..] sub(char[..] s,size_t lo,size_t hi){
return s[lo..hi];
}

(int,bool)find(char[..] hay,char[..] needle){
if(len(needle)==0){
return(true,hay[0..0]);
}
return(false,hay[0..0]);
}

int demo(void){
auto(ok,hit)=find("ab","a");
char[..]*rest;
char[..] s=*rest;
int x;
x=(int)ca-(int)cb;
dst[0]=(char)0;
return(ok,len(hit));
}
