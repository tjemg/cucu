int func( char x  ){
  int tmp;

  tmp = x+1;
  return tmp<<2;
}

int main( ) {
  func(0x44);
  return 0;
}
