#include "EnsambladorIA32.hpp"
#include <cstdint>
#include <cctype>
#include <sstream>
#include <algorithm>
#include <iostream>
#include <iomanip>

using namespace std;

// -----------------------------------------------------------------------------
// Inicialización
// -----------------------------------------------------------------------------

EnsambladorIA32::EnsambladorIA32() : contador_posicion(0) {
    inicializar_mapas();
}

void EnsambladorIA32::inicializar_mapas() {
    // Registros de 32 bits
    reg32_map = {
        {"EAX", 0b000}, {"ECX", 0b001}, {"EDX", 0b010}, {"EBX", 0b011},
        {"ESP", 0b100}, {"EBP", 0b101}, {"ESI", 0b110}, {"EDI", 0b111}
    };

    // Registros de 8 bits
    reg8_map = {
        {"AL", 0b000}, {"CL", 0b001}, {"DL", 0b010}, {"BL", 0b011},
        {"AH", 0b100}, {"CH", 0b101}, {"DH", 0b110}, {"BH", 0b111}
    };
}

// -----------------------------------------------------------------------------
// Utilidades
// -----------------------------------------------------------------------------

void EnsambladorIA32::limpiar_linea(string& linea) {
    // Quitar comentarios
    size_t pos = linea.find(';');
    if (pos != string::npos) linea = linea.substr(0, pos);

    // Trim
    auto no_espacio = [](int ch) { return !isspace(ch); };
    if (!linea.empty()) {
        linea.erase(linea.begin(), find_if(linea.begin(), linea.end(), no_espacio));
        linea.erase(find_if(linea.rbegin(), linea.rend(), no_espacio).base(), linea.end());
    }

    // Mayúsculas
    transform(linea.begin(), linea.end(), linea.begin(), ::toupper);
}

bool EnsambladorIA32::separar_operandos(const string& linea_operandos, string& dest_str, string& src_str) {
    stringstream ss(linea_operandos);
    
    // Leer el destino hasta la primera coma
    if (!getline(ss, dest_str, ',')) return false;

    // Leer el resto como fuente, ignorando espacios iniciales
    ss >> ws;
    if (!getline(ss, src_str)) return false;

    // Limpiar espacios en ambos
    limpiar_linea(dest_str);
    limpiar_linea(src_str);

    // Verificar que la fuente no esté vacía (es decir, que había un src después de la coma)
    return !dest_str.empty() && !src_str.empty();
}

void EnsambladorIA32::agregar_dword(uint32_t dword) {
    agregar_byte(static_cast<uint8_t>(dword & 0xFF));
    agregar_byte(static_cast<uint8_t>((dword >> 8) & 0xFF));
    agregar_byte(static_cast<uint8_t>((dword >> 16) & 0xFF));
    agregar_byte(static_cast<uint8_t>((dword >> 24) & 0xFF));
}

bool EnsambladorIA32::obtener_reg32(const string& op, uint8_t& reg_code) {
    auto it = reg32_map.find(op);
    if (it != reg32_map.end()) {
        reg_code = it->second;
        return true;
    }
    return false;
}

// Direccionamiento simple [ETIQUETA]
bool EnsambladorIA32::procesar_mem_simple(const string& operando,
    uint8_t& modrm_byte,
    const uint8_t reg_code,
    bool es_destino,
    uint8_t op_extension) {
    string op = operando;
    if (op.front() == '[' && op.back() == ']') {
        op = op.substr(1, op.size() - 2);
    }
    else {
        return false;
    }

    string etiqueta = op;

    uint8_t mod = 0b00;
    uint8_t rm = 0b101; // dirección absoluta

    uint8_t reg_field = es_destino ? op_extension : reg_code;

    modrm_byte = generar_modrm(mod, reg_field, rm);
    agregar_byte(modrm_byte);

    // El desplazamiento empieza justo después del ModR/M
    ReferenciaPendiente ref;
    ref.posicion = contador_posicion;
    ref.tamano_inmediato = 4;
    ref.tipo_salto = 0; // absoluto
    referencias_pendientes[etiqueta].push_back(ref);

    agregar_dword(0);  // placeholder
    return true;
}

bool EnsambladorIA32::obtener_inmediato32(const string& str, uint32_t& immediate) {
    string temp_str = str;
    int base = 10;

    // Manejar sufijo H (NASM style: FFFFH)
    if (!temp_str.empty() && temp_str.back() == 'H') {
        temp_str.pop_back();
        base = 16;
    }
    // Manejar prefijo 0X (C/C++ style: 0X80)
    else if (temp_str.size() > 2 && temp_str.substr(0, 2) == "0X") {
        temp_str = temp_str.substr(2); // Eliminar "0X"
        base = 16;
    }
    
    // Pre-chequeo simple: si es solo "H" o "0X", es inválido
    if (temp_str.empty() && base == 16) return false;

    try {
        size_t pos;
        immediate = stoul(temp_str, &pos, base);

        // Si no se consumió toda la cadena, no es un número válido.
        return pos == temp_str.size();
    }
    catch (...) {
        // Captura invalid_argument o out_of_range
        return false;
    }
}
void EnsambladorIA32::agregar_byte(uint8_t byte) {
    codigo_hex.push_back(byte);
    contador_posicion += 1;
}

uint8_t EnsambladorIA32::generar_modrm(uint8_t mod, uint8_t reg, uint8_t rm) {
    return (mod << 6) | (reg << 3) | rm;
}

bool EnsambladorIA32::es_etiqueta(const string& s) {
    // La línea ya está limpia y en mayúsculas
    return !s.empty() && s.back() == ':';
}
void EnsambladorIA32::procesar_etiqueta(const string& etiqueta) {
    // La clave es la etiqueta (ej. "INICIO", "LOOP")
    // El valor es la posición actual del Contador de Posición (CP)
    tabla_simbolos[etiqueta] = contador_posicion;
}
// -----------------------------------------------------------------------------
// Procesamiento de líneas
// -----------------------------------------------------------------------------

void EnsambladorIA32::procesar_linea(string linea) {
    limpiar_linea(linea);
    if (linea.empty()) return;

    if (es_etiqueta(linea)) {
        procesar_etiqueta(linea.substr(0, linea.size() - 1));
        return;
    }

    procesar_instruccion(linea);
}

void EnsambladorIA32::procesar_instruccion(const string& linea) {
    stringstream ss(linea);
    string mnem;
    ss >> mnem;

    string resto;
    getline(ss, resto);
    limpiar_linea(resto); // Limpia el resto de la línea (operandos o directivas)

    // --- 1. IGNORAR DIRECTIVAS ESTÁNDAR (SECTION, GLOBAL, etc.) ---
    // Estas son directivas de NASM que no generan código máquina
    if (mnem == "SECTION" || mnem == "GLOBAL" || mnem == "EXTERN" || mnem == "BITS") {
        return; 
    }

    // --- 2. INSTRUCCIONES IA-32 IMPLEMENTADAS ---
    if (mnem == "MOV") {
        procesar_mov(resto);
    }
    else if (mnem == "ADD") {
        procesar_add(resto);
    }
    else if (mnem == "SUB") {
        procesar_sub(resto);
    }
    else if (mnem == "CMP") {
        procesar_cmp(resto);
    }
    else if (mnem == "JMP") {
        procesar_jmp(resto);
    }
    else if (mnem == "JE" || mnem == "JZ" ||
        mnem == "JNE" || mnem == "JNZ" ||
        mnem == "JLE" || mnem == "JL" ||
        mnem == "JA" || mnem == "JAE" ||
        mnem == "JB" || mnem == "JBE" ||
        mnem == "JG" || mnem == "JGE") {
        procesar_condicional(mnem, resto);
    }
    else if (mnem == "INT") {
        uint32_t immediate;
        // Usamos la utilidad robusta
        if (obtener_inmediato32(resto, immediate) && immediate <= 0xFF) {
            agregar_byte(0xCD);
            agregar_byte(static_cast<uint8_t>(immediate));
        }
        else {
            cerr << "Error: Formato de INT invalido o inmediato fuera de rango (0-255): " << resto << endl;
        }
    }
    // --- 3. ETIQUETAS DE DATOS (RESULTADO DD 0) ---
    else {
        // Si no es una instrucción o directiva conocida, chequeamos si es una ETIQUETA DE DATOS.
        // En este caso, 'mnem' es la ETIQUETA, y el inicio de 'resto' es la DIRECTIVA de datos.
        
        stringstream resto_ss(resto);
        string directiva_dato;
        resto_ss >> directiva_dato;
        limpiar_linea(directiva_dato); // Aseguramos que la directiva esté limpia

        if (directiva_dato == "DD") { // Define Doubleword (4 bytes)
            procesar_etiqueta(mnem); // Agrega la etiqueta (ej. NUMERO, RESULTADO) a tabla_simbolos
            contador_posicion += 4;  // Avanza el CP por el espacio de 4 bytes
            return;
        }
        // Si es DB (Define Byte), usarías:
        // else if (directiva_dato == "DB") { ... contador_posicion += 1; return; }

        // Si falla todo, es una instrucción o directiva realmente no soportada.
        cerr << "Advertencia: Mnemónico o directiva no soportada: " << mnem << endl;
    }
}

// -----------------------------------------------------------------------------
// MOV 
// -----------------------------------------------------------------------------

void EnsambladorIA32::procesar_mov(const string& operandos) {
    string dest_str, src_str;
    if (!separar_operandos(operandos, dest_str, src_str)) {
        cerr << "Error de sintaxis: Se esperaban 2 operandos para MOV." << endl;
        return;
    }

    uint8_t dest_code, src_code;
    bool dest_is_reg = obtener_reg32(dest_str, dest_code);
    bool src_is_reg = obtener_reg32(src_str, src_code);

    // 1. MOV REG, REG (89 r/m32, r32)
    if (dest_is_reg && src_is_reg) {
        agregar_byte(0x89);
        uint8_t modrm = generar_modrm(0b11, src_code, dest_code); // MOD=11 (registro), REG=src, R/M=dest
        agregar_byte(modrm);
        return;
    }

    uint32_t immediate;
    bool src_is_imm = obtener_inmediato32(src_str, immediate);

    // 2. MOV REG, INMEDIATO (B8+rd)
    if (dest_is_reg && src_is_imm) {
        agregar_byte(0xB8 + dest_code);
        agregar_dword(immediate);
        return;
    }

    // Usaremos procesar_mem_simple para [ETIQUETA]
    // 3. MOV [ETIQUETA], REG (89 r/m32, r32 - modo memoria)
    if (src_is_reg) {
        agregar_byte(0x89); // Opcode para r/m32, r32
        uint8_t modrm_byte;
        // es_destino=true porque la memoria es el destino (ModR/M usa REG=src_code)
        if (procesar_mem_simple(dest_str, modrm_byte, src_code, true)) return;
    }

    // 4. MOV REG, [ETIQUETA] (8B r32, r/m32 - modo memoria)
    if (dest_is_reg) {
        agregar_byte(0x8B); // Opcode para r32, r/m32
        uint8_t modrm_byte;
        // es_destino=false porque la memoria es la fuente (ModR/M usa REG=dest_code)
        if (procesar_mem_simple(src_str, modrm_byte, dest_code, false)) return;
    }
    
    // 5. MOV [ETIQUETA], INMEDIATO (C7 /0, imm32)
    if (src_is_imm) {
        agregar_byte(0xC7); // Opcode C7 /0
        uint8_t modrm_byte;
        // El campo REG debe ser 0b000 (extensión de opcode)
        if (procesar_mem_simple(dest_str, modrm_byte, 0b000, true)) {
            agregar_dword(immediate);
            return;
        }
    }

    cerr << "Error de sintaxis o modo no soportado para MOV: " << operandos << endl;
}
// -----------------------------------------------------------------------------
// ADD, SUB, CMP (generalizado)
// -----------------------------------------------------------------------------
void EnsambladorIA32::procesar_binaria(
    const string& mnem,
    const string& operandos,
    uint8_t opcode_rm_reg, // ej: 0x01 (ADD r/m32, r32)
    uint8_t opcode_reg_rm, // ej: 0x03 (ADD r32, r/m32)
    uint8_t opcode_eax_imm, // ej: 0x05 (ADD EAX, imm32)
    uint8_t opcode_imm_general, // ej: 0x81 (ADD r/m32, imm)
    uint8_t reg_field_extension // ej: 0b000 para ADD, 0b101 para SUB
) {
    string dest_str, src_str;
    if (!separar_operandos(operandos, dest_str, src_str)) {
        cerr << "Error de sintaxis: Se esperaban 2 operandos para " << mnem << endl;
        return;
    }

    uint8_t dest_code, src_code;
    bool dest_is_reg = obtener_reg32(dest_str, dest_code);
    bool src_is_reg = obtener_reg32(src_str, src_code);
    
    // 1. REG, REG (r/m32, r32)
    if (dest_is_reg && src_is_reg) {
        agregar_byte(opcode_rm_reg); // ej: 0x01 para ADD, 0x29 para SUB
        uint8_t modrm = generar_modrm(0b11, src_code, dest_code); // MOD=11 (registro), REG=src, R/M=dest
        agregar_byte(modrm);
        return;
    }

    uint32_t immediate;
    bool src_is_imm = obtener_inmediato32(src_str, immediate);

    // 2. EAX, INMEDIATO (opcode dedicado)
    if (dest_is_reg && dest_code == 0b000 && src_is_imm) { // EAX, imm
        agregar_byte(opcode_eax_imm); // ej: 0x05 para ADD, 0x2D para SUB
        agregar_dword(immediate);
        return;
    }

    // Usaremos procesar_mem_simple para [ETIQUETA]

    // 3. REG, [ETIQUETA] (r32, r/m32)
    if (dest_is_reg) {
        agregar_byte(opcode_reg_rm); // ej: 0x03 para ADD, 0x2B para SUB
        uint8_t modrm_byte;
        // es_destino=false porque la memoria es la fuente (ModR/M usa REG=dest_code)
        if (procesar_mem_simple(src_str, modrm_byte, dest_code, false)) return;
    }
    
    // 4. [ETIQUETA], REG (r/m32, r32)
    if (src_is_reg) {
        agregar_byte(opcode_rm_reg); // ej: 0x01 para ADD, 0x29 para SUB
        uint8_t modrm_byte;
        // es_destino=true porque la memoria es el destino (ModR/M usa REG=src_code)
        if (procesar_mem_simple(dest_str, modrm_byte, src_code, true)) return;
    }

    // 5. [ETIQUETA], INMEDIATO (81 /extension, imm32)
    // Usaremos la versión IMM8 (0x83 /extension, imm8) si cabe, ya que el proyecto incluye ejemplos con imm8.
    if (src_is_imm) {
        uint8_t opcode = opcode_imm_general; // ej: 0x81
        bool use_imm8 = (immediate <= 0xFF && immediate >= 0) || (immediate >= 0xFFFFFF80 && immediate <= 0xFFFFFFFF); // Si cabe en 8 bits con extensión de signo
        
        if (use_imm8) opcode = 0x83; // Opcode 0x83 para imm8 sign-extended

        if (!dest_is_reg) { // Memoria, imm
            agregar_byte(opcode);
            uint8_t modrm_byte;
            // El campo REG usa la extensión del opcode, R/M apunta a la memoria
            if (procesar_mem_simple(dest_str, modrm_byte, reg_field_extension, true)) { 
                if (use_imm8) {
                    agregar_byte(static_cast<uint8_t>(immediate & 0xFF));
                } else {
                    agregar_dword(immediate);
                }
                return;
            }
        }
    }
    
    // 6. REG, INMEDIATO (81 /extension, imm32) - Si no es EAX (ya manejado)
    if (dest_is_reg && dest_code != 0b000 && src_is_imm) {
        uint8_t opcode = opcode_imm_general; // ej: 0x81
        bool use_imm8 = (immediate <= 0xFF && immediate >= 0) || (immediate >= 0xFFFFFF80 && immediate <= 0xFFFFFFFF);
        if (use_imm8) opcode = 0x83;

        agregar_byte(opcode);
        uint8_t modrm = generar_modrm(0b11, reg_field_extension, dest_code); // Mod=11 (reg), REG=extensión, R/M=dest
        agregar_byte(modrm);
        
        if (use_imm8) {
            agregar_byte(static_cast<uint8_t>(immediate & 0xFF));
        } else {
            agregar_dword(immediate);
        }
        return;
    }


    cerr << "Error de sintaxis o modo no soportado para " << mnem << ": " << operandos << endl;
}
// -----------------------------------------------------------------------------
// ADD, SUB, CMP (usando el generalizado)
// -----------------------------------------------------------------------------
void EnsambladorIA32::procesar_add(const string& operandos) {
    // 0x01: ADD r/m32, r32; 0x03: ADD r32, r/m32; 0x05: ADD EAX, imm32
    // 0x81: ADD r/m32, imm32; Extensión de opcode: /0 (0b000)
    procesar_binaria("ADD", operandos, 0x01, 0x03, 0x05, 0x81, 0b000);
}

void EnsambladorIA32::procesar_sub(const string& operandos) {
    // 0x29: SUB r/m32, r32; 0x2B: SUB r32, r/m32; 0x2D: SUB EAX, imm32
    // 0x81: SUB r/m32, imm32; Extensión de opcode: /5 (0b101)
    procesar_binaria("SUB", operandos, 0x29, 0x2B, 0x2D, 0x81, 0b101);
}

void EnsambladorIA32::procesar_cmp(const string& operandos) {
    // 0x39: CMP r/m32, r32; 0x3B: CMP r32, r/m32; 0x3D: CMP EAX, imm32
    // 0x81: CMP r/m32, imm32; Extensión de opcode: /7 (0b111)
    procesar_binaria("CMP", operandos, 0x39, 0x3B, 0x3D, 0x81, 0b111);
}

// -----------------------------------------------------------------------------
// Saltos
// -----------------------------------------------------------------------------

void EnsambladorIA32::procesar_jmp(string operandos) {
    limpiar_linea(operandos);
    string etiqueta = operandos;

    agregar_byte(0xE9);     // JMP rel32
    int posicion_referencia = contador_posicion; // aquí va el desplazamiento

    if (tabla_simbolos.count(etiqueta)) {
        int destino = tabla_simbolos[etiqueta];
        int offset = destino - (posicion_referencia + 4);
        agregar_dword(static_cast<uint32_t>(offset));
    }
    else {
        ReferenciaPendiente ref;
        ref.posicion = posicion_referencia;
        ref.tamano_inmediato = 4;
        ref.tipo_salto = 1; // relativo
        referencias_pendientes[etiqueta].push_back(ref);
        agregar_dword(0);
    }
}

void EnsambladorIA32::procesar_condicional(const string& mnem, string operandos) {
    limpiar_linea(operandos);
    string etiqueta = operandos;

    uint8_t opcode_byte1 = 0x0F;
    uint8_t opcode_byte2;

    if (mnem == "JE" || mnem == "JZ")  opcode_byte2 = 0x84;
    else if (mnem == "JNE" || mnem == "JNZ") opcode_byte2 = 0x85;
    else if (mnem == "JLE") opcode_byte2 = 0x8E;
    else if (mnem == "JL")  opcode_byte2 = 0x8C;
    else if (mnem == "JA")  opcode_byte2 = 0x87;
    else if (mnem == "JAE") opcode_byte2 = 0x83;
    else if (mnem == "JB")  opcode_byte2 = 0x82;
    else if (mnem == "JBE") opcode_byte2 = 0x86;
    else if (mnem == "JG")  opcode_byte2 = 0x8F;
    else if (mnem == "JGE") opcode_byte2 = 0x8D;
    else {
        cerr << "Error: Mnemónico condicional no soportado: " << mnem << endl;
        return;
    }

    agregar_byte(opcode_byte1);
    agregar_byte(opcode_byte2);

    int posicion_referencia = contador_posicion; // desplazamiento

    if (tabla_simbolos.count(etiqueta)) {
        int destino = tabla_simbolos[etiqueta];
        int offset = destino - (posicion_referencia + 4);
        agregar_dword(static_cast<uint32_t>(offset));
    }
    else {
        ReferenciaPendiente ref;
        ref.posicion = posicion_referencia;
        ref.tamano_inmediato = 4;
        ref.tipo_salto = 1; // relativo
        referencias_pendientes[etiqueta].push_back(ref);
        agregar_dword(0);
    }
}

// -----------------------------------------------------------------------------
// Resolución de referencias pendientes
// -----------------------------------------------------------------------------

void EnsambladorIA32::resolver_referencias_pendientes() {
    for (auto& par : referencias_pendientes) {
        const string& etiqueta = par.first;
        auto& lista_refs = par.second;

        if (!tabla_simbolos.count(etiqueta)) {
            cerr << "Advertencia: Etiqueta no definida '" << etiqueta
                << "'. Referencia no resuelta." << endl;
            continue;
        }

        int destino = tabla_simbolos[etiqueta];

        for (auto& ref : lista_refs) {
            int pos = ref.posicion;
            uint32_t valor_a_parchear;

            if (ref.tipo_salto == 0) {
                // absoluto
                valor_a_parchear = destino;
            }
            else {
                // relativo
                int offset = destino - (pos + ref.tamano_inmediato);
                valor_a_parchear = static_cast<uint32_t>(offset);
            }

            if (pos + 3 < static_cast<int>(codigo_hex.size())) {
                codigo_hex[pos] = static_cast<uint8_t>(valor_a_parchear & 0xFF);
                codigo_hex[pos + 1] = static_cast<uint8_t>((valor_a_parchear >> 8) & 0xFF);
                codigo_hex[pos + 2] = static_cast<uint8_t>((valor_a_parchear >> 16) & 0xFF);
                codigo_hex[pos + 3] = static_cast<uint8_t>((valor_a_parchear >> 24) & 0xFF);
            }
            else {
                cerr << "Error: Referencia fuera de rango al parchear." << endl;
            }
        }
    }
}

// -----------------------------------------------------------------------------
// Ensamblado y generación de archivos
// -----------------------------------------------------------------------------

void EnsambladorIA32::ensamblar(const string& archivo_entrada) {
    ifstream f(archivo_entrada);
    if (!f.is_open()) {
        cerr << "No se pudo abrir el archivo: " << archivo_entrada << endl;
        return;
    }

    string linea;
    while (getline(f, linea)) {
        procesar_linea(linea);
    }

    f.close();
}

void EnsambladorIA32::generar_hex(const string& archivo_salida) {
    ofstream f(archivo_salida);
    if (!f.is_open()) {
        cerr << "No se pudo abrir archivo de salida: " << archivo_salida << endl;
        return;
    }

    f << hex << uppercase << setfill('0');
    const size_t BYTES_POR_LINEA = 16;
    for (size_t i = 0; i < codigo_hex.size(); ++i) {
        f << setw(2) << static_cast<int>(codigo_hex[i]) << ' ';
        if ((i + 1) % BYTES_POR_LINEA == 0) {
            f << '\n';
        }
    }
    if (codigo_hex.size() % BYTES_POR_LINEA != 0) {
        f << '\n';
    }

    f.close();
}

void EnsambladorIA32::generar_reportes() {
    ofstream sym("simbolos.txt");
    sym << "Tabla de Simbolos:\n";
    for (const auto& par : tabla_simbolos) {
        sym << par.first << " -> " << par.second << '\n';
    }
    sym.close();

    ofstream refs("referencias.txt");
    refs << "Tabla de Referencias Pendientes:\n";
    for (const auto& par : referencias_pendientes) {
        const string& etiqueta = par.first;
        const auto& lista = par.second;
        for (const auto& ref : lista) {
            refs << "Etiqueta: " << etiqueta
                << ", Posicion: " << ref.posicion
                << ", Tamano: " << ref.tamano_inmediato
                << ", Tipo: " << (ref.tipo_salto == 0 ? "ABSOLUTO" : "RELATIVO")
                << '\n';
        }
    }
    refs.close();
}

// -----------------------------------------------------------------------------
// main de prueba
// -----------------------------------------------------------------------------

int main() {
    EnsambladorIA32 ensamblador;

    cout << "Iniciando ensamblado en una sola pasada (leyendo programa.asm)...\n";
    ensamblador.ensamblar("programa.asm");

    cout << "Resolviendo referencias pendientes...\n";
    ensamblador.resolver_referencias_pendientes();

    cout << "Generando programa.hex, simbolos.txt y referencias.txt...\n";
    ensamblador.generar_hex("programa.hex");
    ensamblador.generar_reportes();

    cout << "Proceso finalizado correctamente. Revisa los archivos generados.\n";
    return 0;
}


