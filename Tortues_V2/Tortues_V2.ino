

/*
****************************************************
* Turtoises Overwintering Monitoring  V.1-2024/11/25
****************************************************
*HARDWARE
*
*ARDUINO MEGA board
*Grafic Touch Display 5", IDTB02-5.0, Arduino Mega 2560
*RTC module DS3231 AZDelivery module
*SCD41 Gravity sensor for Temperature, Humidity & CO2 - I2C communication
*
*AZDelivery SPI Reader lector SD TF module memory card shield
*Double LED GREEN/RED common anode 
* Data are in binary data file recorded in the 
* follwing order
* DD =    2 Bytes   > 1 to 31
* MM =    2 Bytes   > 1 to 12
* YY =    2 Bytes   > 24 to 50
* HH =    2 Bytes   > 00 to 24
* Min =   2 Bytes  > 00 to 59
* Tº  =   4 Bytes  > 0 to 254.9  as float
* H%  =   2 Bytes  > 0 to 100
* Co2 =   2 Bytes  > 0 to 5000
* Flag =  2 Bytes  > LSB bits for out of tolerance bit
* TOTAL  20 bytes/recording
*/


#include <URTouch.h>
#include <DFRobot_SCD4X.h>
#include <UTFT.h>
#include <SdFat.h>
#include <time.h>
#include <RTClib.h>
#include <Wire.h>
#include <SPI.h>


DFRobot_SCD4X SCD4X(&Wire, /*i2cAddr = */SCD4X_I2C_ADDR);
RTC_DS3231 RTC;
SdFat SD;
File myFile;
DateTime now;

UTFT myGLCD(ITDB50, 38, 39, 40, 41);
URTouch myTouch (6, 5, 4, 3, 2);


uint8_t i;

float Val_Temp;                     // 0,1º precision
int x, y;
int Val_Day, Val_Month, Val_Year, Val_Hour, Val_Min, Val_Hum, Val_CO2;
int Move_Flag = 0;
int bitPosition;                                   

int Line_Value = 66;                // Line # to display,  @ Start = 66
int Byte_Flag = 0;                  // 3 bits of LSB used as flag for out of tolerances set to 1
                                    // bit 0 -> Temp, bit 1 -> Hum, bit 2 -> CO2
const uint8_t Led_Green = 9;        // Green Led cathode pin number
const uint8_t Led_Red = 10;         // Red Led cathode pin number
const uint8_t Temp_min = 5;        // minimum temperature threshold 
const uint8_t Temp_max = 8;        // Maximum temperature threshold
const uint8_t Hum_min = 60;         // minimum humidity thershols+d
const uint8_t Hum_max = 70;         // manimum humidity threshold
const uint8_t chipSelect = 4;       // Chip Select pin for SD Card, MISO= 50  ,MOSI=51   , SDK= 52   
const unsigned long CO2_max = 800;  // maximum CO2 threshold
uint32_t myFile_Bytes;              // File size
uint32_t Pointer_First_Line;        // SD card address of 1st displayed line on TFT
uint32_t File_Pointer ;             // Address File index to scrolling  start position in the SD File

unsigned long Numb_Rec = 0;               // Number of records

const long interval = 180000;        // To be set by user to own needs  180000ms = 3 min
unsigned long previousMillis = millis();
unsigned long currentMillis = millis();
bool cond ;
bool Flag_UpDown;
bool result;
extern int8_t SmallFont[];
extern int8_t BigFont[];

void setup()
{
   pinMode(Led_Green,OUTPUT);
   pinMode(Led_Red,OUTPUT);
   digitalWrite(Led_Green,HIGH);
   digitalWrite(Led_Red,HIGH);
  
  Serial.begin(115200);
  while (!Serial) 
  {
    // Wait for the serial port to connect (only required for certain boards like Leonardo)
    Serial.println("Waiting for Serial connection...");
    myGLCD.print( "Serial initialization failed!, Exit", 25, 5);
    
    while(1);
    return;
  }
  Serial.println("Serial connected successfully.");
  myGLCD.print( "Serial connected successfully", 25, 5);
  
  myGLCD.InitLCD();
  myGLCD.clrScr();
  myTouch.InitTouch();                // Landscape
  myTouch.setPrecision(PREC_EXTREME);
  myGLCD.setColor(255, 255, 255);    //White
  myGLCD.setBackColor(0, 0, 0);      // Black
  myGLCD.setFont(BigFont);
  
 //   Initialize the SD card
  
  pinMode(53,OUTPUT);
  if (!SD.begin(chipSelect, SPI_HALF_SPEED))
  {
    Serial.println("SD initialization failed!");
    myGLCD.print( "SD initialization failed!, Exit", 25, 30);
    while(1);
    return;
  }
  Serial.println("SD Initialization done.");
  myGLCD.print( "SD Initialization done", 25, 30);

  // Open the file. Note that only one file can be open at a time.
  if(!myFile.open("TORTUES.BIN", O_RDWR | O_CREAT | O_APPEND))
   {
    Serial.println("Failed to open file");
    myGLCD.print("Failed to open file !, Exit", 25, 80);
    while(1);
    return;
   }
    Serial.print("File <TORTUES.BIN> open & File size =   ");
    Serial.println((uint32_t)myFile.fileSize());
    Serial.println("");
    myGLCD.print("File <TORTUES.BIN> created & open ", 25, 80);
    myGLCD.print("File size =  ", 25, 105);
    myGLCD.printNumI(myFile.fileSize(), 208,105);

  // Initialize Sensor
  while( !SCD4X.begin() )
   {
    Serial.println("Sensor communication failed, please check connection");
    myGLCD.print("Sensor communication failed",25,130);
   
    while(1);
    return;
   }
  SCD4X.enablePeriodMeasure(SCD4X_STOP_PERIODIC_MEASURE);
   if(0 != SCD4X.performSelfTest())
   {
    Serial.println("Malfunction detected!");
    myGLCD.print("Sensor not detected!, Exit", 25, 130);
    while(1);
    return;
   }
    myGLCD.print("Sensor detected & OK", 25, 130);
    delay(2000);                // for display reading purpose
   
    SCD4X.setTempComp(4.0);
  
    myGLCD.clrScr();

   Real_Time();
    
    Drawing_TFT(); 
    updateClockDisplay();

//  digitalWrite(Led_Green,LOW);
//  digitalWrite(Led_Red,HIGH);

SCD4X.enablePeriodMeasure(SCD4X_START_PERIODIC_MEASURE);
}
//*************************************************
//*************************************************
void loop()
{
 unsigned long debut = millis();
 Read_Sensor_Data();        
 
 
 Write_SD();
 
 
 
 Display_Master_Line();
 
 
 Byte_Flag = 0;
 Display_Data();              // if not in Scrolling process
 Pointer_First_Line = myFile_Bytes - 20 ;

 Move_Flag = -1;
 Reading_for_Scroll();
 updateClockDisplay();

//==== Special loop for temporizaion (1 minute)
//taking advantage of this loop to simulate interruption when button is
//pressed on TFT screen
//=======  
previousMillis = millis();
   
    do
    {
     if (millis() - previousMillis <= interval) {
        if(Numb_Rec > 21)           // No scrolling available if Records are < =21
          {
            myGLCD.setColor(0,0,0);        // Black
            myGLCD.setBackColor(0,255,0);    // Green
            myGLCD.print("UP", 710,80); 
            myGLCD.print("DOWN",690,260);
            myGLCD.print("Refresh",670,440);
            Arrow_Press();
            cond = true;
          }
        if(currentMillis -previousMillis >= 60000)
          {
            updateClockDisplay();
          }
       }
     else
     if (millis() - previousMillis >= interval) {
       cond = false;
        break;
      }
    } while(cond);
  
    myGLCD.setColor(255,255,255);         // White
    myGLCD.setBackColor(255,0,0);       // Red
    myGLCD.print("UP", 710,80);
    myGLCD.print("DOWN",690,260);
    myGLCD.print("Refresh",670,440);

// ===== End of tempori<ation
 
 //delay(180000);   // Wake up the sensor after 3 minutes, and repeat the above measurement process 
 currentMillis = millis();
  if(currentMillis -previousMillis >= 60000)
    {
      updateClockDisplay();
    }

 
  if (currentMillis - previousMillis >= interval) {
    previousMillis = currentMillis;
    updateClockDisplay();
  }

}

//==============
//     Routines                           
// =============

//***********************
//  Draw Basic TFT layout
// **********************


void Drawing_TFT()
{
  int ii,jj;
  myGLCD.setColor(255, 255, 255);    //White
  myGLCD.setBackColor(0, 0, 0);      // Black
  myGLCD.setFont(BigFont);
  myGLCD.drawRect(0, 0, 662, 479);   // 1er cadre externe
  
  myGLCD.drawLine(2, 2, 660, 2);
  myGLCD.drawLine(2, 2, 2, 477);
  myGLCD.drawLine(660, 2, 660, 477);

  // clock area
    myGLCD.setColor(255, 255, 0);    //Yellow
    myGLCD.fillRoundRect(670,3,793,35);      
  
  // ARROWS
  myGLCD.setColor(255,255,255);     // White
  myGLCD.setBackColor(255,0,0);     // Red
  myGLCD.print("UP", 710,80);     
  ii=0;
  for(jj=100; jj<=150; jj++)
  {
    myGLCD.drawLine(724-ii,jj+1,724+ii,jj+1);   // Drawing UP Arrow
    ii++;
  }
  myGLCD.setColor(255,255,255);     // White
  myGLCD.setBackColor(255,0,0);     // Red
  myGLCD.print("DOWN", 690,260);
  ii=0;
  for(jj=200; jj<=250; jj++)
  {
    myGLCD.drawLine(675+ii,jj+1,773-ii,jj+1);     // Drawing DOWN Arrow
    ii++;
  }
  
  // BUTTON
  myGLCD.fillCircle(725,400, 25);             // Drawing Refresh button
  myGLCD.setColor(255,255,255);     // White
  myGLCD.setBackColor(255,0,0);     // Red
  myGLCD.print("Refresh",670 ,440);
  myGLCD.setColor(255, 255, 255);    //White
  myGLCD.setBackColor(0, 0, 0);      // Black
  
  // Column headers
  myGLCD.drawLine(2, 36, 658, 36);   //  1er ligne horizont TOP line
  myGLCD.drawLine(2, 38, 658, 38);   //  2eme ligne horizont TOP line
  myGLCD.print("DATES", 4, 42);
  myGLCD.print("HH:MM", 134, 42);
  myGLCD.print("TEMP.", 268, 42);
  myGLCD.print("HUM. ", 398, 42);
  myGLCD.print("CO2 ",  528, 42);
  myGLCD.drawLine(2, 60, 658, 60);
  myGLCD.setColor(255,255,255);     // White
  myGLCD.setBackColor(0,0,255);     // Blue
  myGLCD.print( "Waiting for sensor data",134, 10);
  myGLCD.setColor(255, 255, 255);    //White
  myGLCD.setBackColor(0, 0, 0);      // Black
  myGLCD.setFont(SmallFont);
  for ( ii = 60 ;ii <= 460; ii= ii+20)         // draw 20 horizontal lines
  {
   myGLCD.drawLine(2, ii+20, 658, ii+20); 
  }
}

//**********************
// Read Data from sensor
//**********************
void Read_Sensor_Data()
{
  DateTime now = RTC.now();

/**
   * @brief get data ready status
   * @return data ready status:
   * @n        true : data ready
   * @n        false : data not ready
   */
  if(SCD4X.getDataReadyStatus()) {
    /**
     * @brief set ambient pressure
     * @param ambientPressure - the current ambient pressure, unit Pa
     * @return None
     */
    // SCD4X.setAmbientPressure(96000);

    /**
     * @brief Read the measured data
     * @param data - sSensorMeasurement_t, the values measured by the sensor, including CO2 concentration (ppm), temperature (℃) and humidity (RH)
     * @n  typedef struct {
     * @n    uint16_t   CO2ppm;
     * @n    float   temp;
     * @n    float   humidity;
     * @n  } sSensorMeasurement_t;
     * @return None
     * @note CO2 measurement range: 0~40000 ppm; temperature measurement range: -10~60 ℃; humidity measurement range: 0~100 %RH.
     */
    DFRobot_SCD4X::sSensorMeasurement_t data;
    SCD4X.readMeasurement(&data);
    Val_CO2 = data.CO2ppm;
    Val_Temp = data.temp;
    Val_Temp = round(Val_Temp * 10.0)/10.0;
    Val_Hum = data.humidity;
  }
  delay(1000);

  Byte_Flag = 0;
 
  // In/Out tolerance
  if (Val_Temp <Temp_min or Val_Temp > Temp_max)
  {
    bitSet(Byte_Flag,0);
  }
  if (Val_Hum <Hum_min or Val_Hum > Hum_max)
  {
    bitSet(Byte_Flag,1);
  }
  if (Val_CO2 > CO2_max)
  {
    bitSet(Byte_Flag,2);
  }
    
  Val_Day   = now.day();
  Val_Month = now.month();
  Val_Year  = now.year();
  Val_Hour  = now.hour();
  Val_Min   = now.minute();

  Serial.print("Temp = ");
  Serial.print(Val_Temp,1);
  Serial.println(" C");

  Serial.print("Relative humidity : ");
  Serial.print(Val_Hum);
  Serial.println(" RH");

  Serial.print("Carbon dioxide concentration : ");
  Serial.print(Val_CO2);
  Serial.println(" ppm");
}



//****************************
// * Write / Append on SD Card
//****************************
void Write_SD()
{
  myFile.open("TORTUES.BIN",O_WRITE | O_CREAT | O_APPEND);
  
  myFile.write((byte*)&Val_Day, sizeof(Val_Day));
  myFile.write((byte*)&Val_Month, sizeof(Val_Month));
  myFile.write((byte*)&Val_Year, sizeof(Val_Month));
  myFile.write((byte*)&Val_Hour, sizeof(Val_Hour));
  myFile.write((byte*)&Val_Min, sizeof(Val_Min));
  myFile.write((byte*)&Val_Temp, sizeof(Val_Temp));
  myFile.write((byte*)&Val_Hum, sizeof(Val_Hum));
  myFile.write((byte*)&Val_CO2, sizeof(Val_CO2));
  myFile.write((byte*)&Byte_Flag,sizeof(Byte_Flag));

  myFile_Bytes = (uint32_t)myFile.fileSize();
  Numb_Rec = (uint32_t)myFile.fileSize()/20 ;
  
  Serial.print("\nAfter recording on SD File,  size =  ");
  Serial.print((uint32_t)myFile.fileSize());
  Serial.print("  bytes & Numb_Rec =  ");
  Serial.println(Numb_Rec );
  Serial.println("");

  myFile.flush();
  myFile.close();
}


//****************************
// Write on Master_Line on TOP 
//****************************
void Display_Master_Line()
{
  myGLCD.setFont(BigFont);
  myGLCD.setBackColor(0,0,0);   // Black
  myGLCD.setColor(0,0,0);       // Black
  myGLCD.print("                       ",CENTER,10);  //"Waiting for sensor data"
  myGLCD.setBackColor(0,0,255); // Blue
  myGLCD.setColor(255,255,255); // White
 if(Val_Day <= 9)
  {
    myGLCD.print("0",4,10);
    myGLCD.printNumI(Val_Day,20,10);
  }
  else
  {
    myGLCD.printNumI(Val_Day,4,10);
  }
  
  myGLCD.print("/", 36,10);
  
  if (Val_Month <= 9)
  {
    myGLCD.print("0",52,10);
    myGLCD.printNumI(Val_Month, 68,10);
  }
  else 
  {
    myGLCD.printNumI(Val_Month, 52,10);
  }
  
  if (Val_Hour <= 9)
  {
    myGLCD.print("0",134,10);
    myGLCD.printNumI(Val_Hour, 150,10);
  }
  else 
  {
    myGLCD.printNumI(Val_Hour, 134, 10);     
  }
  
  myGLCD.print(":", 166,10);
  if(Val_Min <= 9)
  {
    myGLCD.print("0",182,10);
    myGLCD.printNumI(Val_Min,198,10);
  }
  else
  {
    myGLCD.printNumI(Val_Min,182,10);
  }

  myGLCD.printNumF(Val_Temp,1, 268, 10);     
  myGLCD.print(" C", 332, 10);
  myGLCD.printNumI(Val_Hum,398, 10);       
  myGLCD.print(" %", 430, 10);
  if(Val_CO2 >= 1000)
  {
    myGLCD.printNumI(Val_CO2,512,10);
  }
  else
  {
    myGLCD.print(" ",512,10);
    myGLCD.printNumI(Val_CO2, 528, 10);       
  }
  myGLCD.print(" /ppm", 576, 10);
  myGLCD.setColor(255, 255, 255);          //White
  myGLCD.setBackColor(0, 0, 0);            // Black

  Led_Swap();
}

//*************************
// Display Data on one line
//*************************
void Display_Data()               // Remenmber that Line_Value = 66 @ first pass in the Loop
{

  myGLCD.setBackColor(0,0,0);           //Black
  myGLCD.setColor(255, 255, 255);      // White
  myGLCD.setFont(SmallFont);            //8x12
   
  if(Val_Day <= 9)
    {
      myGLCD.print("0",12 ,Line_Value);
      myGLCD.printNumI(Val_Day,20, Line_Value);
    }
    else
    {
      myGLCD.printNumI(Val_Day,12, Line_Value);
    }
    
  myGLCD.print("/",28,Line_Value);
  
  if(Val_Month <= 9)
    {
      myGLCD.print("0",36,Line_Value);
      myGLCD.printNumI(Val_Month, 44, Line_Value);
    }
    else
    {
      myGLCD.printNumI(Val_Month, 36, Line_Value);
    }

  myGLCD.print("/",52,Line_Value);
  myGLCD.printNumI(Val_Year-2000, 60, Line_Value);
 
  if(Val_Hour <= 9)
    {
      myGLCD.print("0",156, Line_Value);
      myGLCD.printNumI(Val_Hour,164,Line_Value);
    }
    else
    {
      myGLCD.printNumI(Val_Hour,156,Line_Value);
    }
    myGLCD.print(":",172   ,Line_Value);
  if (Val_Min <= 9)
    {
      myGLCD.print("0",180,Line_Value);
      myGLCD.printNumI(Val_Min,188, Line_Value);
    }
    else
    {
      myGLCD.printNumI(Val_Min,180,Line_Value);
    }

  
  myGLCD.setBackColor(0,0,0);
  
  bitPosition = 0;
  result = testBit(Byte_Flag,bitPosition);
  if (result == true)
   {
     myGLCD.setBackColor(255,0,0);                 // Red
   }
  if(Val_Temp <= 9 )
  {
    myGLCD.print(" ",282 ,Line_Value);
    myGLCD.printNumF(Val_Temp, 1,290, Line_Value);
  }
  else
  {
    myGLCD.printNumF(Val_Temp, 1,282, Line_Value);
  }
   myGLCD.setBackColor(0,0,0);                     //Black
  
  bitPosition = 1;
  result = testBit(Byte_Flag,bitPosition);
  if (result == true) 
  {
    myGLCD.setBackColor(255,0,0);                 //Red
  }
  myGLCD.printNumI(Val_Hum, 410, Line_Value);
  myGLCD.setBackColor(0,0,0);                     //Black

  
  bitPosition = 2;
  result = testBit(Byte_Flag,bitPosition);
  if (result == true) 
  {
    myGLCD.setBackColor(255,0,0);                 //Red
  }
 
  
  if(Val_CO2 <=999)
    {
      myGLCD.print(" ",539,Line_Value);    
      myGLCD.printNumI(Val_CO2, 547, Line_Value);
    }
    else
    {
      myGLCD.printNumI(Val_CO2,539,Line_Value);
    }
     myGLCD.setBackColor(0,0,0);                     //Black
    
    myGLCD.setFont(BigFont);         
}

//***********************
//test bit
//**********************
bool testBit(byte octet, int n){
  return(octet>> n) & 1;
}

//********************************************
// Fetch data from SD Card & call Display_Data
//********************************************

void Reading_for_Scroll()
{
    
   myFile.open("TORTUES.BIN", O_RDONLY);
   
    uint8_t Shift;                                 // Number of lines to move
    if(Numb_Rec == 1)
    {
      myFile.close();
      return;
    }
    if(Numb_Rec >= 2 && Numb_Rec <= 21)
    {
      Shift = Numb_Rec;
    }
    else if (Numb_Rec >= 22)
    {
      Shift = 21;
    }

   
   File_Pointer = Pointer_First_Line;             
   Line_Value =66;

   for (i= Shift; i>= 1 ; i --)                         
    {
   
      myFile.seekSet(File_Pointer);
      myFile.read((byte*)&Val_Day, sizeof(Val_Day));
      myFile.read((byte*)&Val_Month, sizeof(Val_Month));
      myFile.read((byte*)&Val_Year, sizeof(Val_Month));
      myFile.read((byte*)&Val_Hour, sizeof(Val_Hour));
      myFile.read((byte*)&Val_Min, sizeof(Val_Min));
      myFile.read((byte*)&Val_Temp, sizeof(Val_Temp));
      myFile.read((byte*)&Val_Hum, sizeof(Val_Hum));
      myFile.read((byte*)&Val_CO2, sizeof(Val_CO2));
      myFile.read((byte*)&Byte_Flag,sizeof(Byte_Flag));        
  
      Display_Data();
      Line_Value = Line_Value + 20;            // 20 = line step
      File_Pointer = File_Pointer + (20* Move_Flag);                                                   
      }
      myFile.close();
      
}

//*************************************************
// Check if arrow pressed
//**************************************************

void Arrow_Press()
{

  if(myTouch.dataAvailable())
  {
    myGLCD.setFont(BigFont);
    myTouch.read();
    x = myTouch.getX();
    y = myTouch.getY();
      
      if((x >= 675) && (x <= 773))        
      {
        if((y >= 100 && y <= 150))      //"UP Arrow", move lines from top to bottom, towards the most recent record
         {
 
            myGLCD.setFont(BigFont);
            myGLCD.setColor(0,0,0);             // Black
            myGLCD.setBackColor(255,255,0);     // Yellow
            myGLCD.print("UP", 710,80); 
            // move lines to bottom, towards the the most recent record
            // checking if the first line display = last record 
            if (Pointer_First_Line == myFile_Bytes -20)      // no more scrolling possible
            {
             // Flag_UpDown = 1;
              Long_Buzz(1);
             
              return;
            }
            
            Move_Flag = -1;
            Pointer_First_Line = Pointer_First_Line + 20 ;
            
            Reading_for_Scroll();
            while (myTouch.dataAvailable());
            myGLCD.setColor(0,0,0);          // Black
            myGLCD.setBackColor(0,255,0);    // Green
            myGLCD.print("UP", 710,80); 

         }
      else if((y >= 200 && y <= 250))   // Down Arrow,  move lines from bottom to top, towards the oldest records
         {
            myGLCD.setFont(BigFont);
            myGLCD.setColor(0,0,0);             // Black
            myGLCD.setBackColor(255,255,0);     // Yellow
            myGLCD.print("DOWN", 690,260); 
            // move lines to top, towards the oldest record
            // checking if the first line display = last first record 
            if (Pointer_First_Line == 0)      // no more scrolling possible
            {
              Long_Buzz(0);
              return;
            }
            
            Move_Flag = -1;
            Pointer_First_Line = Pointer_First_Line + (20 * Move_Flag);
            Reading_for_Scroll();
            while (myTouch.dataAvailable());
            myGLCD.setColor(0,0,0);          // Black
            myGLCD.setBackColor(0,255,0);    // Green
            myGLCD.print("DOWN", 690,260);
 
         }
          
      else if((y >= 375) && (y <= 425))     //Round button          // Refresh
         {
            if((x >= 700)&& (x <= 750))
             {
                myGLCD.setFont(BigFont);
                myGLCD.setColor(0,0,0);             // Black
                myGLCD.setBackColor(255,255,0);     // Yellow
                myGLCD.print("Refresh", 670,440); 
                // Move back to actual TFT 
                Pointer_First_Line = myFile_Bytes - 20;
                   
                Reading_for_Scroll();
                while (myTouch.dataAvailable());
                myGLCD.setColor(0,0,0);        // Black
                myGLCD.setBackColor(0,255,0);    // Green
                myGLCD.print("Refresh", 670,440); 

             }
          }
      }
   }
}

 
//******************************
// * READ RTC data from module
//***************************** 


void Real_Time()
{
 if (! RTC.begin()) {
    Serial.println("Couldn't find RTC");
    Serial.flush();
    abort();
  }

  // When time needs to be re-set on a previously configured device, the
  // following line sets the RTC to the date & time this sketch was compiled
  // RTC.adjust(DateTime(F(__DATE__), F(__TIME__)));
  // This line sets the RTC with an explicit date & time, for example to set
  // January 21, 2014 at 3am you would call:
  //RTC.adjust(DateTime(2024, 11, 25, 18, 9, 0));

    DateTime now = RTC.now();
    Serial.print(now.day());
    Serial.print("/");
    Serial.print(now.month());
    Serial.print("/");
    Serial.print(now.year());
    Serial.print("   ");
    
    Serial.print(now.hour());
    Serial.print(":");
    Serial.println(now.minute());
}

//***********************************
// * Clock area display
//********************************* 

void updateClockDisplay()
{
  DateTime now = RTC.now();
  myGLCD.setColor(0,0,0);      // Black
  myGLCD.setBackColor(255,255,0); //Yellow
  myGLCD.setFont(BigFont);
  if (now.hour() <= 9)
  {
    myGLCD.print("0",692,12);
    myGLCD.printNumI(now.hour(), 708,12);
  }
  else 
  {
    myGLCD.printNumI(now.hour(), 692, 12);     
  }
  
  myGLCD.print(":", 724,12);
  if(now.minute() <= 9)
  {
    myGLCD.print("0",740,12);
    myGLCD.printNumI(now.minute(),756,12);
  }
  else
  {
    myGLCD.printNumI(now.minute(),740,12);
  }
}
//*****************
// Long_Buzz alert
//*****************
void Long_Buzz(bool UpDown)
{
  // Not allowed operation
   myGLCD.setColor(255,255,255);    // White
   
   myGLCD.setBackColor(255,0,0);    // Red
   switch (UpDown)
   {
    case 0:
      myGLCD.print("DOWN",710,80);
      break;
    case 1:
      myGLCD.print("UP",710,80);
      break;
   }
   
   delay(600);
   myGLCD.setColor(0,0,0);          // Black
   myGLCD.setBackColor(0,255,0);    // Green
   myGLCD.print("UP",710,80);
}

//*****************************
// LED_Swap
//****************************

void Led_Swap()
{
  if (testBit(Byte_Flag,0) == true or testBit(Byte_Flag,1) == true or testBit(Byte_Flag,2) == true)
  { 
    
    digitalWrite(Led_Green,HIGH);       
    digitalWrite(Led_Red,LOW);
  }
  else 
  if (testBit(Byte_Flag,0) == false && testBit(Byte_Flag,1) == false && testBit(Byte_Flag,2) == false)
  { 
    
    digitalWrite(Led_Green,LOW);
    digitalWrite(Led_Red,HIGH);
  }
}


//**************************************
// Pour Test
//***************************************
void Read_Check()
{
  myFile.open("TORTUES.BIN", O_RDONLY);
  File_Pointer = myFile.fileSize() -20;
  for(int i = 0; i <= 19; i++)
    {
      myFile.seekSet(File_Pointer);
      myFile.read((byte*)&Val_Day, sizeof(Val_Day));
      myFile.read((byte*)&Val_Month, sizeof(Val_Month));
      myFile.read((byte*)&Val_Year, sizeof(Val_Month));
      myFile.read((byte*)&Val_Hour, sizeof(Val_Hour));
      myFile.read((byte*)&Val_Min, sizeof(Val_Min));
      myFile.read((byte*)&Val_Temp, sizeof(Val_Temp));
      myFile.read((byte*)&Val_Hum, sizeof(Val_Hum));
      myFile.read((byte*)&Val_CO2, sizeof(Val_CO2));
      myFile.write((byte*)&Byte_Flag,sizeof(Byte_Flag));
      
      
      
      Serial.print(i);
      Serial.print("==");
      Serial.print(Val_Day); 
      Serial.print("\t ");
      Serial.print (Val_Month);
      Serial.print("\t ");
      Serial.print (Val_Year);

      Serial.print("\t ");
      Serial.print(Val_Hour);
      Serial.print("\t ");
      Serial.print (Val_Min);
 
      Serial.print("\t ");
      Serial.print (Val_Temp);
      Serial.print("\t ");
      Serial.print (Val_Hum);
      Serial.print("\t ");
      Serial.print(Val_CO2);

      Serial.print("\t ");
      Serial.println(Byte_Flag);
      File_Pointer = File_Pointer -20;
    }
  
}
 
