int g_a;

int func( int a ) {
  g_a = 1;
}

//int main( int c0, int c1, int c2, int c3 ) {
int main( void ) {
  int a;
  int b;
//  a = 0x4f;
  func(0x44);
  a = func(0x44);
  a = 1 + func(2+0x44);
  return 0;
}

