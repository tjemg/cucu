int func( char x  ){
  int tmp; // hello

  tmp = x+1;
  tmp = x ^ 3;
  return tmp<<2;
}

int main( void ) {
  func(0x44);
  return 0;
}
