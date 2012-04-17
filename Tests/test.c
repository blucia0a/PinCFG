#include <stdio.h>
#include <stdlib.h>

void foo(int a){

  int i;
  int q;
  for( i = 0; i < a; i++){
    q++;    
  }

  if( q > 10 ){
    printf("yay!\n");
  }else{
    printf("nay!\n");
  }
  
}

int main(int argc, char *argv[]){

  if( argc > 0 ){
    foo( atoi(argv[1]) );
  }else{
    foo( 100 );
  }
  return;
}
