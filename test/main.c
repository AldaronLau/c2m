#include <stdint.h>
#include <stdio.h>
static void io__print(char*  string){
fputs(string, stdout);
}
static void io__println(char*  string){
puts(string);
}
int main(int argc, char* argv[]){
int32_t v = 190;
io__print("Start...");
io__println("Hello World = ");
return 0; }
