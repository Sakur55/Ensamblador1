#ifndef ENSAMBLADOR_IA32_HPP
#define ENSAMBLADOR_IA32_HPP

#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <unordered_map>
#include <algorithm>
#include <iomanip>
#include <cstdint>

using namespace std;

// --- ESTRUCTURAS DE DATOS ---
// Estructura para almacenar una referencia pendiente
struct ReferenciaPendiente {
    int posicion;           // Posición en codigo_hex donde se necesita el parche
    int tamano_inmediato;   // 1 o 4 (byte o dword para el desplazamiento)
    int tipo_salto;         // 0: Absoluto (dirección de etiqueta), 1: Relativo (dirección de salto)
};

class EnsambladorIA32 {
private:
    int contador_posicion; // Contador de posición (CP)
    unordered_map<string, int> tabla_simbolos; // Etiqueta -> Dirección (CP)
    unordered_map<string, vector<ReferenciaPendiente>> referencias_pendientes; // Etiqueta -> Lista de refs
    vector<uint8_t> codigo_hex; // Código máquina generado

    unordered_map<string, uint8_t> reg32_map; // Códigos de 32-bit (EAX=0, ECX=1, ...)
    unordered_map<string, uint8_t> reg8_map;  // Códigos de 8-bit

    // --- MÉTODOS AUXILIARES ---
    void inicializar_mapas();
    void limpiar_linea(string& linea);
    bool es_etiqueta(const string& s);

    void procesar_linea(string linea);
    void procesar_etiqueta(const string& etiqueta);
    void procesar_instruccion(const string& linea);

    void procesar_mov(const string& operandos);
    void procesar_add(const string& operandos);
    void procesar_sub(const string& operandos);
    void procesar_jmp(string operandos); // por valor para poder limpiarla
    void procesar_condicional(const string& mnem, string operandos); // por valor para poder limpiarla

    // --- UTILIDADES DE CODIFICACIÓN ---
    uint8_t generar_modrm(uint8_t mod, uint8_t reg, uint8_t rm);
    void agregar_byte(uint8_t byte);
    void agregar_dword(uint32_t dword); // Para inmediatos y desplazamientos de 32 bits
    bool obtener_reg32(const string& op, uint8_t& reg_code);
    bool procesar_mem_simple(const string& operando,
                             uint8_t& modrm_byte,
                             const uint8_t reg_code,
                             bool es_destino,
                             uint8_t op_extension = 0);

public:
    EnsambladorIA32();

    void ensamblar(const string& archivo_entrada);
    void resolver_referencias_pendientes();
    void generar_hex(const string& archivo_salida);
    void generar_reportes();
};

#endif // ENSAMBLADOR_IA32_HPP
 
