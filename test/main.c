#include <stdio.h>
void io__print(char*  string ){
fputs(string, stdout);
}
int main(int argc, char* argv[]){
io__print("Testing...\n");
return 0; }
