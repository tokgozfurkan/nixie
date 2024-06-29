/*
01010000011100100110111101110100011011110010000001000111

Nixie Tube Demo
This code cycles through the digits of a Nixie Tube.

Created April 20, 2015
Modified April 20, 2015
by Anthony Garofalo (Proto G)

Visit my YouTube channel here: https://www.youtube.com/channel/UCpTuKJrXFwybnpOG10HpTpZw
Visit my Instructables page here: http://www.instructables.com/member/Proto+G/

  _____   ______  _____  _______  _____        ______
 |_____] |_____/ |     |    |    |     |      |  ____
 |       |    \_ |_____|    |    |_____|      |_____|
  
01010000011100100110111101110100011011110010000001000111
*/





void setup() {
 
  pinMode(11, OUTPUT);// D
  pinMode(10, OUTPUT);// C
  pinMode(9, OUTPUT);// B
  pinMode(8, OUTPUT);// A

  pinMode(7, OUTPUT);// D
  pinMode(6, OUTPUT);// C
  pinMode(5, OUTPUT);// B
  pinMode(4, OUTPUT);// A

  pinMode(3, OUTPUT);// D
  pinMode(2, OUTPUT);// C
  pinMode(1, OUTPUT);// B
  pinMode(0, OUTPUT);// A
  
 
}
 
void loop() {
  
 //0 
  digitalWrite(11, LOW);  //D
  digitalWrite(10, LOW);  //C
  digitalWrite(9, LOW);  //B
  digitalWrite(8, LOW);  //A

  digitalWrite(7, LOW);  //D
  digitalWrite(6, LOW);  //C
  digitalWrite(5, LOW);  //B
  digitalWrite(4, LOW);  //A

  digitalWrite(3, LOW);  //D
  digitalWrite(2, LOW);  //C
  digitalWrite(1, LOW);  //B
  digitalWrite(0, LOW);  //A
 
 delay(1000);
 //1
  digitalWrite(11, LOW);  //D
  digitalWrite(10, LOW);  //C
  digitalWrite(9, LOW);  //B
  digitalWrite(8, HIGH); //A

  digitalWrite(7, LOW);  //D
  digitalWrite(6, LOW);  //C
  digitalWrite(5, LOW);  //B
  digitalWrite(4, HIGH);  //A

  digitalWrite(3, LOW);  //D
  digitalWrite(2, LOW);  //C
  digitalWrite(1, LOW);  //B
  digitalWrite(0, HIGH);  //A

 delay(1000);
 //2
  digitalWrite(11, LOW);  //D
  digitalWrite(10, LOW);  //C
  digitalWrite(9, HIGH); //B
  digitalWrite(8, LOW);  //A

  digitalWrite(7, LOW);  //D
  digitalWrite(6, LOW);  //C
  digitalWrite(5, HIGH);  //B
  digitalWrite(4, LOW);  //A

  digitalWrite(3, LOW);  //D
  digitalWrite(2, LOW);  //C
  digitalWrite(1, HIGH);  //B
  digitalWrite(0, LOW);  //A

 delay(1000);
 //3
  digitalWrite(11, LOW);  //D
  digitalWrite(10, LOW);  //C
  digitalWrite(9, HIGH); //B
  digitalWrite(8, HIGH); //A

  digitalWrite(7, LOW);  //D
  digitalWrite(6, LOW);  //C
  digitalWrite(5, HIGH);  //B
  digitalWrite(4, HIGH);  //A

  digitalWrite(3, LOW);  //D
  digitalWrite(2, LOW);  //C
  digitalWrite(1, HIGH);  //B
  digitalWrite(0, HIGH);  //A

 delay(1000);
 //4
  digitalWrite(11, LOW);  //D
  digitalWrite(10, HIGH); //C
  digitalWrite(9, LOW);  //B
  digitalWrite(8, LOW);  //A

  digitalWrite(7, LOW);  //D
  digitalWrite(6, HIGH);  //C
  digitalWrite(5, LOW);  //B
  digitalWrite(4, LOW);  //A

  digitalWrite(3, LOW);  //D
  digitalWrite(2, HIGH);  //C
  digitalWrite(1, LOW);  //B
  digitalWrite(0, LOW);  //A


 delay(1000);
 //5
  digitalWrite(11, LOW);  //D
  digitalWrite(10, HIGH); //C
  digitalWrite(9, LOW);  //B
  digitalWrite(8, HIGH); //A

  digitalWrite(7, LOW);  //D
  digitalWrite(6, HIGH);  //C
  digitalWrite(5, LOW);  //B
  digitalWrite(4, HIGH);  //A

  digitalWrite(3, LOW);  //D
  digitalWrite(2, HIGH);  //C
  digitalWrite(1, LOW);  //B
  digitalWrite(0, HIGH);  //A


 delay(1000);
 //6
  digitalWrite(11, LOW);  //D
  digitalWrite(10, HIGH); //C
  digitalWrite(9, HIGH); //B
  digitalWrite(8, LOW);  //A

  digitalWrite(7, LOW);  //D
  digitalWrite(6, HIGH);  //C
  digitalWrite(5, HIGH);  //B
  digitalWrite(4, LOW);  //A

  digitalWrite(3, LOW);  //D
  digitalWrite(2, HIGH);  //C
  digitalWrite(1, HIGH);  //B
  digitalWrite(0, LOW);  //A


 delay(1000);
 //7
  digitalWrite(11, LOW);  //D
  digitalWrite(10, HIGH); //C
  digitalWrite(9, HIGH); //B
  digitalWrite(8, HIGH); //A

  digitalWrite(7, LOW);  //D
  digitalWrite(6, HIGH);  //C
  digitalWrite(5, HIGH);  //B
  digitalWrite(4, HIGH);  //A

  digitalWrite(3, LOW);  //D
  digitalWrite(2, HIGH);  //C
  digitalWrite(1, HIGH);  //B
  digitalWrite(0, HIGH);  //A


 delay(1000);
 //8
  digitalWrite(11, HIGH); //D
  digitalWrite(10, LOW);  //C
  digitalWrite(9, LOW);  //B
  digitalWrite(8, LOW);  //A

  digitalWrite(7, HIGH);  //D
  digitalWrite(6, LOW);  //C
  digitalWrite(5, LOW);  //B
  digitalWrite(4, LOW);  //A

  digitalWrite(3, HIGH);  //D
  digitalWrite(2, LOW);  //C
  digitalWrite(1, LOW);  //B
  digitalWrite(0, LOW);  //A

 delay(1000);
 //9
  digitalWrite(11, HIGH); //D
  digitalWrite(10, LOW);  //C
  digitalWrite(9, LOW); //B
  digitalWrite(8, HIGH);  //A

  digitalWrite(7, HIGH);  //D
  digitalWrite(6, LOW);  //C
  digitalWrite(5, LOW);  //B
  digitalWrite(4, HIGH);  //A

  digitalWrite(3, HIGH);  //D
  digitalWrite(2, LOW);  //C
  digitalWrite(1, LOW);  //B
  digitalWrite(0, HIGH);  //A

 delay(1000);
 
 
 
  
}
