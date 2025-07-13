#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <ESP8266WiFi.h> 
#include <ESP8266HTTPClient.h> 
#include <WiFiClientSecure.h> 
#include <ArduinoJson.h> 

// --- CONFIGURACIÓN WIFI ---
const char* ssid = ""; 
const char* password = ""; 

const char* serverUrl = ""; 

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// --- Configuración del Botón de Entrada Morse ---
const int PIN_BOTON_MORSE_ANALOGO = A0; 
const int UMBRAL_PULSACION = 250; 

// --- Pin para el Botón de Borrar Todo ---
const int PIN_BOTON_BORRAR_TODO = D6;
bool botonBorrarPresionadoAnterior = false;

// --- Pin para el Botón de ENVIAR MENSAJE ---
const int PIN_BOTON_ENVIAR = D5; 
bool botonEnviarPresionadoAnterior = false; 

// --- Pin para el Foco de 6.3V (en D0) ---
const int PIN_FOCO_HTTP = D0; 

// --- Pines para los LEDs de estado (D1 para éxito, D2 para error) ---
const int PIN_LED_EXITO = D1; // LED verde (GPIO5)
const int PIN_LED_ERROR = D2; // LED rojo (GPIO4)


// --- Variables de Estado de la Aplicación ---
enum AppState {
  STATE_NORMAL_INPUT,
  STATE_SENDING,        
  STATE_SHOW_RESULT     
};
AppState currentState = STATE_NORMAL_INPUT; 

// --- Variables para la Lógica del Código Morse ---
long tiempoPulsado = 0;      
long tiempoSoltado = 0;      
bool botonMorsePresionado = false; 

// Tiempos base del código Morse (en milisegundos)
const int UNIDAD_TIEMPO = 150; 
const int TIEMPO_RAYA = UNIDAD_TIEMPO * 3; 
const int TIEMPO_ENTRE_LETRAS = UNIDAD_TIEMPO * 3; 
const int TIEMPO_PAUSA_ENVIO = UNIDAD_TIEMPO * 10; 

// Variables para almacenar la entrada Morse y el mensaje
String entradaMorseActual = ""; 
String mensajeTraducido = "";   
long ultimaActividadMorse = 0; 
long tiempoInicioEstadoTemporal = 0; 
const long DURACION_MENSAJE_RESULTADO = 1000; // Duración del mensaje de resultado y LEDs

// --- Diccionario de Código Morse a Texto y Acciones Especiales ---
String morseMap[][2] = {
    {".-", "A"}, {"-...", "B"}, {"-.-.", "C"}, {"-..", "D"}, {".", "E"},
    {"..-.", "F"}, {"--.", "G"}, {"....", "H"}, {"..", "I"}, {".---", "J"},
    {"-.-", "K"}, {".-..", "L"}, {"--", "M"}, {"-.", "N"}, {"---", "O"},
    {".--.", "P"}, {"--.-", "Q"}, {".-.", "R"}, {"...", "S"}, {"-", "T"},
    {"..-", "U"}, {"...-", "V"}, {".--", "W"}, {"-..-", "X"}, {"-.--", "Y"},
    {"--..", "Z"},
    {"-----", "0"}, {".----", "1"}, {"..---", "2"}, {"...--", "3"}, {"....-", "4"},
    {".....", "5"}, {"-....", "6"}, {"--...", "7"}, {"---..", "8"}, {"----.", "9"},
    {"-.-.--", "!"}, {".-.-.-", "."}, {"--..--", ","}, {"...---...", "SOS"},
    {".-.-", " "},      // Código Morse para ESPACIO
    {"-...-", "<DEL>"},  // Código Morse para BORRAR ÚLTIMA LETRA
};
const int MORSE_MAP_SIZE = sizeof(morseMap) / sizeof(morseMap[0]);


// --- FUNCIONES DE UTILIDAD ---

// Función para generar un ID aleatorio de 6 dígitos
String generarID() {
  String id = "";
  for (int i = 0; i < 6; i++) {
    id += String(random(0, 10)); 
  }
  return id;
}

// Función para traducir Morse a texto
String traducirMorse(String morse) {
  for (int i = 0; i < MORSE_MAP_SIZE; i++) {
    if (morseMap[i][0] == morse) {
      return morseMap[i][1];
    }
  }
  return "?";
}

// Función para apagar ambos LEDs de estado
void apagarLedsEstado() {
  digitalWrite(PIN_LED_EXITO, LOW);
  digitalWrite(PIN_LED_ERROR, LOW);
}

// Función para encender LED de éxito
void encenderLedExito() {
  apagarLedsEstado();
  digitalWrite(PIN_LED_EXITO, HIGH);
}

// Función para encender LED de error
void encenderLedError() {
  apagarLedsEstado();
  digitalWrite(PIN_LED_ERROR, HIGH);
}


// Función para enviar la solicitud HTTP POST
void enviarPeticionHTTP(String message) {
  if (WiFi.status() == WL_CONNECTED) {
    // --- CAMBIOS CLAVE AQUÍ PARA HTTPS ---
    WiFiClientSecure client; // Usar WiFiClientSecure en lugar de WiFiClient
    
    // !!! PELIGROSO - SOLO PARA PRUEBAS !!!
    // Esta línea desactiva la verificación del certificado SSL.
    // No la uses en un entorno de producción real, ya que compromete la seguridad.
    client.setInsecure(); 
    // !!! FIN DEL PELIGRO !!!

    HTTPClient http;

    String id = generarID(); 

    StaticJsonDocument<200> doc; 
    doc["message"] = message; // Solo la clave "message"
    // doc["id"] = id;       // Ya comentada, lo cual es correcto

    String jsonPayload;
    serializeJson(doc, jsonPayload);

    Serial.print("Intentando enviar POST a: ");
    Serial.println(serverUrl);
    Serial.print("Cuerpo JSON: ");
    Serial.println(jsonPayload);

    http.begin(client, serverUrl); // Asegúrate de pasar 'client' aquí
    http.addHeader("Content-Type", "application/json"); 

    int httpResponseCode = http.POST(jsonPayload); 

    digitalWrite(PIN_FOCO_HTTP, HIGH); // Apaga el foco justo después de que la petición HTTP finaliza
    
    if (httpResponseCode > 0) { 
      String response = http.getString();
      Serial.print("Código de respuesta HTTP: ");
      Serial.println(httpResponseCode);
      Serial.print("Respuesta del servidor: ");
      Serial.println(response);

      Serial.println("¡Peticion HTTP exitosa!");
      display.clearDisplay();
      display.setTextSize(2); 
      display.setCursor(0,25);
      display.println("ENVIADO!");
      display.display();
      encenderLedExito(); // Enciende LED verde
      currentState = STATE_SHOW_RESULT;
      tiempoInicioEstadoTemporal = millis(); 

    } else { 
      Serial.print("Error en la peticion HTTP: ");
      Serial.println(httpResponseCode);
      Serial.println(http.errorToString(httpResponseCode)); // Muestra el error de la librería
      Serial.println("¡Error al enviar la peticion!");
      display.clearDisplay();
      display.setTextSize(2); 
      display.setCursor(0,10);
      display.println("ERROR");
      display.setCursor(0,35);
      display.println("AL ENVIAR");
      display.display();
      encenderLedError(); // Enciende LED rojo
      currentState = STATE_SHOW_RESULT;
      tiempoInicioEstadoTemporal = millis(); 
    }
    http.end(); 
  } else { 
    Serial.println("¡WiFi no conectado! No se pudo enviar la peticion HTTP.");
    display.clearDisplay();
    display.setTextSize(2); 
    display.setCursor(0,25);
    display.println("WIFI DESCON.");
    display.display();
    encenderLedError(); // Enciende LED rojo si no hay WiFi
    currentState = STATE_SHOW_RESULT; 
    tiempoInicioEstadoTemporal = millis();
  }
}

// --- SETUP ---
void setup() {
  Serial.begin(115200);
  randomSeed(analogRead(A0)); 

  Serial.println("Iniciando Telegrafo Morse con Funcionalidad HTTP...");

  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println(F("SSD1306 allocation failed"));
    for(;;); 
  }

  // Configurar pines de LEDs como OUTPUT y apagarlos
  pinMode(PIN_LED_EXITO, OUTPUT);
  pinMode(PIN_LED_ERROR, OUTPUT);
  apagarLedsEstado(); // Asegúrate de que ambos estén apagados al inicio

  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE); 
  display.setTextSize(2); 
  display.setCursor(0,0);
  display.println("INICIANDO...");
  display.display();
  delay(1000); 

  // --- Conexión WiFi ---
  WiFi.begin(ssid, password);
  Serial.print("Conectando a WiFi.");
  
  display.clearDisplay();
  display.setTextSize(2); 
  display.setCursor(0,0);
  display.println("CONECTANDO");
  display.println("WIFI");
  display.display(); 

  int dots = 0; 
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
    
    if (dots < 3) { 
      display.print("."); 
      display.display();
      dots++;
    } else { 
      display.clearDisplay();
      display.setTextSize(2);
      display.setCursor(0,0);
      display.println("CONECTANDO");
      display.println("WIFI");
      display.setCursor(0, display.getCursorY()); 
      dots = 0;
    }
  }
  Serial.println("\n¡Conectado a WiFi!");
  Serial.print("Direccion IP: ");
  Serial.println(WiFi.localIP());

  display.clearDisplay();
  display.setCursor(0,0);
  display.setTextSize(2); 
  display.println("WIFI");
  display.println("CONECTADO!");
  display.setTextSize(1); 
  display.setCursor(0, display.getCursorY()); 
  display.println(WiFi.localIP());
  display.display();
  delay(3000); 
  
  display.clearDisplay();
  display.setCursor(0,0);
  display.setTextSize(2); 
  display.println("LISTO!");
  display.display();
  delay(1000); 

  Serial.println("Pin del boton Morse configurado para lectura analogica.");
  pinMode(PIN_BOTON_MORSE_ANALOGO, INPUT); 
  pinMode(PIN_BOTON_BORRAR_TODO, INPUT_PULLUP);
  Serial.println("Pin para el boton de borrar todo configurado como INPUT_PULLUP (D6).");
  
  pinMode(PIN_BOTON_ENVIAR, INPUT_PULLUP);
  Serial.println("Pin para el boton de enviar configurado como INPUT_PULLUP (D5).");

  pinMode(PIN_FOCO_HTTP, OUTPUT);
  digitalWrite(PIN_FOCO_HTTP, HIGH); 
  Serial.print("Pin para el foco HTTP configurado como OUTPUT (");
  Serial.print(PIN_FOCO_HTTP);
  Serial.println(" - D0).");

  ultimaActividadMorse = millis();
  currentState = STATE_NORMAL_INPUT; 
  actualizarOLED(); 
}

// --- ACTUALIZAR PANTALLA OLED ---
void actualizarOLED() {
  if (currentState == STATE_NORMAL_INPUT) {
    display.clearDisplay();
    display.setTextColor(SSD1306_WHITE);
    apagarLedsEstado(); // Apaga los LEDs de estado cuando vuelves a la entrada normal

    display.setTextSize(1); 
    display.setCursor(0,0);
    display.println("Morse:"); 
    display.println(entradaMorseActual); 
    display.println("---");
    
    display.setTextSize(2); 
    display.print("Msg: "); 
    
    int charsPerLine = SCREEN_WIDTH / (2 * 6); 
    int maxDisplayChars = charsPerLine * 2; 
    
    if (mensajeTraducido.length() > maxDisplayChars) {
      display.println(mensajeTraducido.substring(mensajeTraducido.length() - maxDisplayChars));
    } else {
      display.println(mensajeTraducido); 
    }
    display.display();
  } 
}

// --- LOOP PRINCIPAL ---
void loop() {
  // Manejo de la conexión WiFi (reconexión si se pierde)
  if (WiFi.status() != WL_CONNECTED) {
    display.clearDisplay();
    display.setCursor(0,0);
    display.setTextSize(2); 
    display.println("WIFI DESCON."); 
    display.println("RECONECTANDO."); 
    display.display();
    encenderLedError(); // Enciende el LED rojo si se pierde la conexión WiFi
    delay(100); 
    WiFi.begin(ssid, password); 
    return; 
  } else {
    // Si la conexión WiFi está OK, y no estamos en un estado de envío/resultado,
    // podemos apagar el LED de error si estaba encendido por desconexión previa.
    if (currentState == STATE_NORMAL_INPUT) {
      apagarLedsEstado(); 
    }
  }

  switch (currentState) {
    case STATE_NORMAL_INPUT: {
      int valorAnalogico = analogRead(PIN_BOTON_MORSE_ANALOGO);
      bool estadoBotonMorseActual = (valorAnalogico > UMBRAL_PULSACION);

      if (estadoBotonMorseActual && !botonMorsePresionado) { 
        tiempoPulsado = millis();
        botonMorsePresionado = true;
        ultimaActividadMorse = millis(); 
      } else if (!estadoBotonMorseActual && botonMorsePresionado) { 
        tiempoSoltado = millis();
        botonMorsePresionado = false;
        ultimaActividadMorse = millis(); 

        long duracionPulsacion = tiempoSoltado - tiempoPulsado;

        if (duracionPulsacion >= TIEMPO_RAYA) {
          entradaMorseActual += "-"; 
        } else if (duracionPulsacion > 0) { 
          entradaMorseActual += "."; 
        }
        actualizarOLED(); 
      }

      if (!botonMorsePresionado && entradaMorseActual.length() > 0) {
        long tiempoInactividad = millis() - ultimaActividadMorse;

        if (tiempoInactividad >= TIEMPO_ENTRE_LETRAS) { 
          String caracterTraducido = traducirMorse(entradaMorseActual); 

          if (caracterTraducido == "<DEL>") { 
            if (mensajeTraducido.length() > 0) {
              int lastCharIndex = mensajeTraducido.length() - 1;
              while (lastCharIndex >= 0 && mensajeTraducido.charAt(lastCharIndex) == ' ') {
                  lastCharIndex--;
              }
              if (lastCharIndex >= 0) { 
                  mensajeTraducido.remove(lastCharIndex, 1);
              }
              Serial.println("  BORRAR ULTIMA LETRA (Morse) detectado.");
            } else {
              Serial.println("  BORRAR ULTIMA LETRA (Morse) - Mensaje vacio.");
            }
          } else if (caracterTraducido == " ") { 
            if (mensajeTraducido.length() == 0 || mensajeTraducido.charAt(mensajeTraducido.length() - 1) != ' ') {
                mensajeTraducido += " ";
                Serial.println("  ESPACIO (Morse) detectado.");
            }
          } else if (caracterTraducido != "?") { 
            mensajeTraducido += caracterTraducido; 
            Serial.print("  Letra traducida: ");
            Serial.println(caracterTraducido);
          } else { 
            Serial.print("  Secuencia Morse no reconocida: ");
            Serial.println(entradaMorseActual);
          }

          entradaMorseActual = ""; 
          actualizarOLED(); 
        }
      }

      bool estadoBotonBorrarActual = (digitalRead(PIN_BOTON_BORRAR_TODO) == LOW);

      if (estadoBotonBorrarActual && !botonBorrarPresionadoAnterior) { 
        mensajeTraducido = "";     
        entradaMorseActual = "";   
        Serial.println(">>> Boton de BORRAR TODO activado. Mensaje Reiniciado."); 
        actualizarOLED(); 
        delay(200); 
      }
      botonBorrarPresionadoAnterior = estadoBotonBorrarActual; 

      bool estadoBotonEnviarActual = (digitalRead(PIN_BOTON_ENVIAR) == LOW); 

      if (estadoBotonEnviarActual && !botonEnviarPresionadoAnterior) { 
        if (mensajeTraducido.length() > 0) { 
          Serial.println("--- Boton de ENVIAR PRESIONADO. Iniciando peticion HTTP...");
          display.clearDisplay();
          display.setTextSize(2); 
          display.setCursor(0,25);
          display.println("ENVIANDO...");
          display.display();
          digitalWrite(PIN_FOCO_HTTP, LOW); 
          apagarLedsEstado(); // Apaga los LEDs mientras envía
          currentState = STATE_SENDING; 
          
          enviarPeticionHTTP(mensajeTraducido); 
          mensajeTraducido = ""; 
          entradaMorseActual = ""; 
          
        } else {
          Serial.println("--- No hay mensaje para enviar.");
        }
        delay(200); 
      }
      botonEnviarPresionadoAnterior = estadoBotonEnviarActual;
      break; 
    }

    case STATE_SENDING: {
      break;
    }

    case STATE_SHOW_RESULT: {
      if (millis() - tiempoInicioEstadoTemporal >= DURACION_MENSAJE_RESULTADO) {
        currentState = STATE_NORMAL_INPUT;
        actualizarOLED(); 
        Serial.println("Volviendo a la entrada Morse normal.");
      }
      break;
    }
  }

  delay(10); 
}
