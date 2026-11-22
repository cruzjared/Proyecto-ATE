#include <WiFi.h>
#include <ThingSpeak.h>


WiFiClient cliente;


//INFO COMUNICACION
const int numeroCanal = 3580998;
const char* APIKey = "5X8Z4H7207S7TK7T"; 
const char* ssid = "Wokwi-GUEST";   
const char* password = "";   

//features
float lecturaLuz = 0; 
int lecturaMovimiento = 0; 
int lecturaPotenciometro = 0; 
int decibeles = 0;
int ocupacionReal = 0; 
int ocupacionEstimada = 0; 
///----------
unsigned long ultimaLectura = 0; 
unsigned long ultimoEnvio = 0;
const float GAMMA = 0.7;
const float RL10 = 50;


//VARIABLES ESTIMACION
/*Una persona puede entrar al salon
moverse y no hacer ruido, o hacer ruido y no moverse.
Por ello, se utilizan dos condiciones para estimar ocupacion:
ruido y movimiento. Para asegurar que las personas sigan ahi
aunque esten en silencio o sin moverse, se realizaran actualizaciones
de la estimacion cada 2 minutos o 120,000 milisegundos*/
const int UMBRAL_DECIBELES = 50;
const unsigned long LIMITE_DETECCION = 120000;
unsigned long ultimaDeteccion = 0;
bool deteccion = false;

//ACUMULADORES PARA ENVIOS 
// POR VENTANA DE 15 SEGUNDOS
int segundosMovimiento = 0;
long sumaDb = 0;
int maxDb = 0;
float sumaLuz = 0;
int lecturasVentana = 0;

//PINES COMPONENTES
const uint8_t FOCO = 23; 
const uint8_t MOVIMIENTO = 19; 
const uint8_t LUZ_ANALOGO = 35; 
const uint8_t POTENCIOMETRO = 34; 
const uint8_t BUTTON = 18; 

// GESTION BOTON
int estadoBotonAnterior = HIGH;
int estadoBotonEstable = HIGH;
unsigned long ultimoCambioBoton = 0;
const unsigned long TIEMPOSEGURO = 100;


 


void setup() {



  Serial.begin(115200);
  inicializacion();
  inicializarWiFi(); 
  ThingSpeak.begin(cliente); 

}

void loop() {
  
  unsigned long tiempoActual = millis(); 

  gestionarBoton(); 
  if(ocupacionReal == 1)
    digitalWrite(FOCO, HIGH);
  else
    digitalWrite(FOCO, LOW);

  if(tiempoActual - ultimaLectura >= 1000)
  {
    leerSensores();
    actualizarEstimacion();
    imprimirLecturas();
    ultimaLectura = tiempoActual; 
  }


  if(tiempoActual - ultimoEnvio > 16000)
  {
    enviarDatos();
    ultimoEnvio = tiempoActual;
  }

}



void inicializarWiFi(){
  WiFi.begin(ssid, password,6);

  while(WiFi.status() != WL_CONNECTED){
    delay(500);
  }
}


void inicializacion(){

  pinMode(FOCO, OUTPUT);

  pinMode(MOVIMIENTO, INPUT);

  pinMode(BUTTON, INPUT_PULLUP);

  digitalWrite(FOCO, LOW);


}

void leerSensores()
{

  lecturaLuz = luzEnLux(analogRead(LUZ_ANALOGO));

  lecturaMovimiento = digitalRead(MOVIMIENTO);

  lecturaPotenciometro = analogRead(POTENCIOMETRO);

  //simular la lectura del potenciometro dentro del rango de decibeles
  decibeles = map(lecturaPotenciometro, 0, 4095, 30, 120);

  segundosMovimiento += lecturaMovimiento;
  sumaDb += decibeles;
  if (decibeles > maxDb) maxDb = decibeles;
  sumaLuz += lecturaLuz;
  lecturasVentana++;  

}

float luzEnLux(int lecturaAnaloga)
{

  float voltaje = lecturaAnaloga / 4095.0 * 3.3;

    // Evitar divisiones entre cero
    if (voltaje <= 0.0) 
      return 0;
    
    float resistencia = 10000.0 * voltaje / (3.3 - voltaje);

    float iluminacion = pow(RL10 * 1e3 * pow(10, GAMMA) / resistencia,1.0 / GAMMA);

    return iluminacion;



}


void imprimirLecturas() {

    Serial.println("----- LECTURAS DE SENSORES -----");

    Serial.print("Luz: ");
    Serial.println(lecturaLuz);

    Serial.print("Movimiento: ");
    Serial.println(lecturaMovimiento);

    Serial.print("Actividad acustica simulada: ");
    Serial.println(decibeles);

    Serial.print("Ocupacion real: ");
    Serial.println(ocupacionReal);

    Serial.print("Estimacion de ocupacion:");
    Serial.println(ocupacionEstimada);

    Serial.println("--------------------------------");
}


void gestionarBoton()
{

  int lectura = digitalRead(BUTTON);

  if (lectura != estadoBotonAnterior) 
    ultimoCambioBoton = millis();
    
  //prevenir dobles clicks
  if (millis() - ultimoCambioBoton >= TIEMPOSEGURO)
  {

    if (lectura != estadoBotonEstable)
    {

      estadoBotonEstable = lectura;
      // Detectar pulsacion
      // Si se detecta un pulsacion
      // se niega la ocupacion real (invierte valor)
      if (estadoBotonEstable == LOW)
        ocupacionReal = !ocupacionReal;
      
    }
  }

  estadoBotonAnterior = lectura;
  




}


void actualizarEstimacion()
{


  if(lecturaMovimiento == HIGH || decibeles > UMBRAL_DECIBELES)
  {
    ultimaDeteccion = millis();
    deteccion = true; 
  }


  if(deteccion && millis() - ultimaDeteccion < LIMITE_DETECCION)
    ocupacionEstimada = 1;
  else
    ocupacionEstimada = 0;



}


void enviarDatos()
{

  if(lecturasVentana == 0)
    return;


  unsigned long seg = (millis() - ultimaDeteccion)/1000; 
  //si no hay detecciones
  // los segundos suben sin tope
  // se establece tope de 10 minutos sin detección
  if(!deteccion || seg > 600)
    seg = 600; 

  ThingSpeak.setField(1,sumaLuz/lecturasVentana);
  ThingSpeak.setField(2, segundosMovimiento);
  ThingSpeak.setField(3, (float)sumaDb/lecturasVentana);
  ThingSpeak.setField(4, ocupacionReal);
  ThingSpeak.setField(5, ocupacionEstimada);
  ThingSpeak.setField(6,maxDb);
  ThingSpeak.setField(7, (int)seg);

  int x = ThingSpeak.writeFields(numeroCanal,APIKey);

  if(x == 200)
    Serial.println("Data pushed successfull");
  else
    Serial.println("Push error" + String(x));


  segundosMovimiento = 0;
  sumaDb = 0;
  maxDb = 0;
  sumaLuz = 0;
  lecturasVentana = 0;
      



}