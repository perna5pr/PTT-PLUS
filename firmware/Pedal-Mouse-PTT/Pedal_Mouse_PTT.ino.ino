#include <HijelHID_BLEMouse.h>

HijelBLEMouse mouse;

int inPin_R = 3; 
int limite = 0;

void setup() {
    mouse.begin();
 pinMode(inPin_R, INPUT);
}

void loop() {
    if (mouse.isPaired()) {
   
        int val_R = digitalRead(inPin_R);   // lê o pino de entrada

        if (limite < 1) {
            delay(1000); 
            mouse.moveTo(100, -100);
            delay(1000);
            mouse.moveTo(100, -100);
            delay(1000);       
         }

       limite = 2;
          if (val_R == LOW ) {// se valor está em zero( tecla pressionada)         
            //mouse.click(MouseButton::Left);
            mouse.press(MouseButton::Left);
            // delay(200);
            // mouse.release(MouseButton::Left);
            //  mouse.setButton(MouseButton::Left, pressed);
            delay(50);} else {
           mouse.release(MouseButton::Left);            
          }





    }
}