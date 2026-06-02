#define IR_BPLUS 0xF700FF  
#define IR_BMINUS 0xF7807F 
#define IR_OFF 0xF740BF    
#define IR_ON 0xF7C03F     
#define IR_R 0xF720DF      
#define IR_G 0xF7A05F      
#define IR_B 0xF7609F      
#define IR_W 0xF7E01F      
#define IR_B1 0xF710EF     
#define IR_B2 0xF7906F     
#define IR_B3 0xF750AF     
#define IR_FLASH 0xF7D02F  
#define IR_B4 0xF730CF     
#define IR_B5 0xF7B04F     
#define IR_B6 0xF7708F     
#define IR_STROBE 0xF7F00F 
#define IR_B7 0xF708F7     
#define IR_B8 0xF78877     
#define IR_B9 0xF748B7     
#define IR_FADE 0xF7C837   
#define IR_B10 0xF728D7    
#define IR_B11 0xF7A857    
#define IR_B12 0xF76897    
#define IR_SMOOTH 0xF7E817 


typedef struct {
  uint64_t code;
  int r, g, b;
} IRColor;

IRColor colors[] = {
  { IR_R, 255, 0, 0 },
  { IR_G, 0, 255, 0 },
  { IR_B, 0, 0, 255 },
  { IR_W, 255, 255, 255 },
  { IR_B1, 235, 40, 7 },
  { IR_B2, 27, 167, 70 },
  { IR_B3, 4, 126, 210 },
  { IR_B4, 235, 66, 11 },
  { IR_B5, 33, 197, 200 },
  { IR_B6, 88, 29, 71 },
  { IR_B7, 244, 134, 23 },  // orange
  { IR_B8, 2, 137, 151 },
  { IR_B9, 127, 35, 83 },
  { IR_B10, 254, 235, 50 },  // yellow
  { IR_B11, 8, 90, 99 },     // cyan
  { IR_B12, 204, 50, 94 }
};