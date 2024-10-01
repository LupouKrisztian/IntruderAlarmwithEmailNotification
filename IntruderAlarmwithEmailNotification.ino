#include <WiFi.h>
#include <Wire.h> 
#include <ESP_Mail_Client.h>

#define WIFI_TIMEOUT_MS 10000       // 10 second WiFi connection timeout
#define WIFI_RECOVER_TIME_MS 5000   // Wait 5 seconds after a failed connection attempt

#define WIFI_SSID ""
#define WIFI_PASSWORD ""

#define SMTP_HOST "smtp.gmail.com"
#define SMTP_PORT 465

/* The sign in credentials */
#define AUTHOR_EMAIL ""
#define AUTHOR_PASSWORD ""

/* Recipient's email*/
#define RECIPIENT_EMAIL1 ""
#define RECIPIENT_EMAIL2 ""

/* The SMTP Session object used for Email sending */
SMTPSession smtp;

TaskHandle_t Task1;
TaskHandle_t Task2;

int pirPin = 25;                 // PIR Out pin 
int pirStat = LOW;               // PIR status
int pirVal = 0;                  // PIR value
unsigned long lastDetection = 0;
unsigned long runSeconds = 0;
bool connectedToWiFi = false; 
bool isSentAfterWakeup = false;
RTC_DATA_ATTR int bootCount = 0;

/* Callback function to get the Email sending status */
void smtpCallback(SMTP_Status status);

void sendEmail()
{
   /** Enable the debug via Serial port
   * none debug or 0
   * basic debug or 1
  */
  smtp.debug(1);

  /* Set the callback function to get the sending results */
  smtp.callback(smtpCallback);

  /* Declare the session config data */
  ESP_Mail_Session session;

  /* Set the session config */
  session.server.host_name = SMTP_HOST;
  session.server.port = SMTP_PORT;
  session.login.email = AUTHOR_EMAIL;
  session.login.password = AUTHOR_PASSWORD;
  session.login.user_domain = "";

  /* Declare the message class */
  SMTP_Message message;

  /* Set the message headers */
  message.sender.name = "ESP";
  message.sender.email = AUTHOR_EMAIL;
  message.subject = "Alarm!!!";
  message.addRecipient("RECIPIENT1", RECIPIENT_EMAIL1);
  message.addRecipient("RECIPIENT2", RECIPIENT_EMAIL2);

  /*Send HTML message*/
  String htmlMsg = "<div style=\"color:#2f4468;\"><h1>Atention!</h1><p>Motion detected!</p></div>";
  message.html.content = htmlMsg.c_str();
  message.html.content = htmlMsg.c_str();
  message.text.charSet = "us-ascii";
  message.html.transfer_encoding = Content_Transfer_Encoding::enc_7bit;

  /* Connect to server with the session config */
  if (!smtp.connect(&session))
    return;

  /* Start sending Email and close the session */
  if (!MailClient.sendMail(&smtp, &message))
    Serial.println("Error sending Email, " + smtp.errorReason());
}

/* Callback function to get the Email sending status */
void smtpCallback(SMTP_Status status)
{
  /* Print the current status */
  Serial.println(status.info());

  /* Print the sending result */
  if (status.success())
  {
    Serial.println("----------------");
    ESP_MAIL_PRINTF("Message sent success: %d\n", status.completedCount());
    ESP_MAIL_PRINTF("Message sent failled: %d\n", status.failedCount());
    Serial.println("----------------\n");
    struct tm dt;

    for (size_t i = 0; i < smtp.sendingResult.size(); i++)
    {
      /* Get the result item */
      SMTP_Result result = smtp.sendingResult.getItem(i);
      time_t ts = (time_t)result.timestamp;
      localtime_r(&ts, &dt);

      ESP_MAIL_PRINTF("Message No: %d\n", i + 1);
      ESP_MAIL_PRINTF("Status: %s\n", result.completed ? "success" : "failed");
      ESP_MAIL_PRINTF("Date/Time: %d/%d/%d %d:%d:%d\n", dt.tm_year + 1900, dt.tm_mon + 1, dt.tm_mday, dt.tm_hour, dt.tm_min, dt.tm_sec);
      ESP_MAIL_PRINTF("Recipient: %s\n", result.recipients);
      ESP_MAIL_PRINTF("Subject: %s\n", result.subject);
    }
    Serial.println("----------------\n");
  }
}

void print_wakeup_reason()
{
  esp_sleep_wakeup_cause_t wakeup_reason;

  wakeup_reason = esp_sleep_get_wakeup_cause();

  switch(wakeup_reason)
  {
    case ESP_SLEEP_WAKEUP_EXT0 : Serial.println("Wakeup caused by external signal using RTC_IO"); break;
    case ESP_SLEEP_WAKEUP_EXT1 : Serial.println("Wakeup caused by external signal using RTC_CNTL"); break;
    case ESP_SLEEP_WAKEUP_TIMER : Serial.println("Wakeup caused by timer"); break;
    case ESP_SLEEP_WAKEUP_TOUCHPAD : Serial.println("Wakeup caused by touchpad"); break;
    case ESP_SLEEP_WAKEUP_ULP : Serial.println("Wakeup caused by ULP program"); break;
    default : Serial.printf("Wakeup was not caused by deep sleep: %d\n",wakeup_reason); break;
  }
}

void send_email_after_wakeup()
{
  esp_sleep_wakeup_cause_t wakeup_reason;

  wakeup_reason = esp_sleep_get_wakeup_cause();

  if (wakeup_reason == ESP_SLEEP_WAKEUP_EXT0)
    sendEmail();

  isSentAfterWakeup = true;
}

void Task1code( void * pvParameters )
{
  Serial.print("Task1 (WiFi connection) running on core ");
  Serial.println(xPortGetCoreID());
  
  for(;;)
  {
    if (WiFi.status() == WL_CONNECTED) 
    {
        vTaskDelay(10000 / portTICK_PERIOD_MS);
        continue;
    }

    connectedToWiFi = false;
    
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    Serial.println("Connecting to WiFi...");

    unsigned long startAttemptTime = millis();
     
    while (WiFi.status() != WL_CONNECTED && millis() - startAttemptTime < WIFI_TIMEOUT_MS){}

    if(WiFi.status() != WL_CONNECTED)
    {
       Serial.println("WiFi failed to connect");
       vTaskDelay(WIFI_RECOVER_TIME_MS / portTICK_PERIOD_MS);
       continue;
    }
    
    Serial.println("");   
    Serial.print("Connected to ");
    Serial.println(WIFI_SSID); 
    
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());
    
    Serial.print("RSSI: ");
    Serial.println(WiFi.RSSI());
    Serial.println("");

    connectedToWiFi = true;
  } 
}

void Task2code( void * pvParameters )
{
  Serial.print("Task2 (Sensor and Email Sender) running on core ");
  Serial.println(xPortGetCoreID());
  
  for(;;)
  {
    /* If in the last 5 minutes no motion is detected, ESP32 will go to sleep */
    runSeconds = millis()/1000;
    if (runSeconds - lastDetection > 300)
    {
      Serial.println("Going to sleep now");
      vTaskDelay(1000 / portTICK_PERIOD_MS);
      esp_deep_sleep_start();  
    }

    if (!isSentAfterWakeup && connectedToWiFi)
      send_email_after_wakeup();
      
    pirVal = digitalRead(pirPin); 
    if (pirVal == HIGH)   
    {
      vTaskDelay(100 / portTICK_PERIOD_MS);          
      if (pirStat == LOW) 
      {
        lastDetection = millis()/1000;
        Serial.println("Motion detected!"); 
        pirStat = HIGH;       // update variable state to HIGH
        if (connectedToWiFi)
        {  
          sendEmail();
        }
      }
    }
    else
    {
      vTaskDelay(200 / portTICK_PERIOD_MS);           
      if (pirStat == HIGH)
      {
        Serial.println("Motion stopped!");
        pirStat = LOW;       // update variable state to LOW
      } 
    } 
    vTaskDelay(200 / portTICK_PERIOD_MS);
  }
}

void setup(){
  pinMode(pirPin, INPUT);  
  Serial.begin(115200);

  ++bootCount;
  Serial.println("Boot number: " + String(bootCount));

  print_wakeup_reason();
  esp_sleep_enable_ext0_wakeup(GPIO_NUM_25,1);

  xTaskCreatePinnedToCore(
                    Task1code,                      /* Task function. */
                    "Task1",                        /* name of task. */
                    3000,                           /* Stack size of task */
                    NULL,                           /* parameter of the task */
                    1,                              /* priority of the task */
                    &Task1,                         /* Task handle to keep track of created task */
                    CONFIG_ARDUINO_RUNNING_CORE);   /* pin task to core 1 */                  
  delay(1000); 

  xTaskCreatePinnedToCore(
                    Task2code,   /* Task function. */
                    "Task2",     /* name of task. */
                    6000,        /* Stack size of task */
                    NULL,        /* parameter of the task */
                    1,           /* priority of the task */
                    &Task2,      /* Task handle to keep track of created task */
                    0);          /* pin task to core 0 */
  delay(500); 
}

void loop(){
  delay(2);
}
