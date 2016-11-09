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
C2M_WHILE1:
io__println("I'm inside a while loop!");
goto C2M_WHILE1;
return 0; }
