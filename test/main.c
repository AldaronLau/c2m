#include <stdio.h>
static void io__print(char*  string){
fputs(string, stdout);
}
static void io__println(char*  string){
puts(string);
}
int main(int argc, char* argv[]){
io__print("Start...");
io__println("Hello World = 190");
return 0; }
