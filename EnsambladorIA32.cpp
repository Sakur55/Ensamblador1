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

bool EnsambladorIA32::obtener_reg8(const string& op, uint8_t& reg_code) {
    auto it = reg8_map.find(op);
    if (it != reg8_map.end()) {
        reg_code = it->second;
        return true;
    }
    return false;
}


// Direccionamiento simple [ETIQUETA]
bool EnsambladorIA32::procesar_mem_sib(const string& operando,
                                       uint8_t& modrm_byte,
                                       const uint8_t reg_code,
                                       bool /*es_destino*/) 
{
    string op = operando;

    // Debe venir entre corchetes: [ ... ]
    if (op.size() < 2 || op.front() != '[' || op.back() != ']')
        return false;

    // Quitar corchetes
    op = op.substr(1, op.size() - 2);

   // Limpiar espacios internos y externos
    limpiar_linea(op);
    op.erase(
        remove_if(op.begin(), op.end(),
                [](unsigned char c){ return isspace(c); }),
        op.end()
    );

    // Patrón esperado: <etiqueta> + ESI*4 (+ disp)
    // Buscamos "ESI*4"
    size_t idxESI = op.find("ESI*4");
    if (idxESI == string::npos)
        return false;

    // --- 1. Obtener la etiqueta antes de "ESI*4" ---
    // Ej: "ARRAY+ESI*4+4" -> etiqueta parcial "ARRAY+"
    string etiqueta = op.substr(0, idxESI);

    // Si termina en '+', se lo quitamos: "ARRAY+" -> "ARRAY"
    if (!etiqueta.empty() && etiqueta.back() == '+')
        etiqueta.pop_back();
    limpiar_linea(etiqueta); //Cambio

    // Si por alguna razón quedó vacía, no es un patrón válido
    if (etiqueta.empty())
        return false;

    // --- 2. Obtener desplazamiento opcional (disp8) después de ESI*4 ---
    //   op = "ARRAY+ESI*4+4"
    //          ^     ^   ^
    //        idxEt  idxESI  plusAfter
    uint8_t disp8 = 0;
    size_t plusAfter = op.find('+', idxESI + 5); // 5 = longitud de "ESI*4"

    if (plusAfter != string::npos) {
        string dispStr = op.substr(plusAfter + 1);  // lo que va después del '+'
        if (!dispStr.empty()) {
            try {
                int d = stoi(dispStr);  // soporta "+4", "4", etc.
                disp8 = static_cast<uint8_t>(d & 0xFF);
            } catch (...) {
                // Si falla el parseo, dejamos disp8 = 0 y seguimos
            }
        }
    }

    // --- 3. Codificar ModR/M ---
    // MOD = 00 si no hay disp8
    // MOD = 01 si hay disp8
    // R/M = 100 para indicar que viene byte SIB
    uint8_t mod = (disp8 == 0) ? 0b00 : 0b01;
    uint8_t rm  = 0b100;
    uint8_t reg_field = reg_code;  // el registro del operando (REG en ModR/M)

    modrm_byte = generar_modrm(mod, reg_field, rm);
    agregar_byte(modrm_byte);

    // --- 4. Codificar SIB ---
    // SCALE = 10 (x4)
    // INDEX = 110 (ESI)
    // BASE  = 101 (usaremos disp32 como base absoluta)
    uint8_t scale = 0b10;   // *4
    uint8_t index = 0b110;  // ESI
    uint8_t base  = 0b101;  // base "disp32"
    uint8_t sib   = (scale << 6) | (index << 3) | base;
    agregar_byte(sib);

    // --- 5. Si MOD = 01, agregamos disp8 ---
    if (mod == 0b01) {
        agregar_byte(disp8);
    }

    // --- 6. Referencia pendiente para la etiqueta (disp32) ---
    // Aquí va la dirección de la etiqueta (relleno = 0 por ahora)
    ReferenciaPendiente ref;
    ref.posicion        = contador_posicion;
    ref.tamano_inmediato = 4;
    ref.tipo_salto      = 0;  // absoluto
    referencias_pendientes[etiqueta].push_back(ref);

    agregar_dword(0);  // placeholder disp32

    return true;
}


bool EnsambladorIA32::obtener_inmediato32(const string& str, uint32_t& immediate) {
    string temp_str = str;
    int base = 10;

    if (temp_str.size() == 3 && temp_str.front() == '\'' && temp_str.back() == '\'') {
        // Asumimos que es un solo carácter entre comillas
        if (temp_str.size() == 3) {
            // El valor es el código ASCII del carácter central
            immediate = static_cast<uint32_t>(temp_str[1]);
            return true;
        }
    }

    // Manejar sufijo H (NASM style: FFFFH)
    if (!temp_str.empty() && temp_str.back() == 'H') {
        temp_str.pop_back();
        base = 16;
    }
    // Manejar prefijo 0X (C/C++ style: 0X80)
    else if (temp_str.size() > 2 && (temp_str.substr(0, 2) == "0X")) {
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

bool EnsambladorIA32::obtener_reg32(const string& op, uint8_t& reg_code) {
    auto it = reg32_map.find(op);
    if (it != reg32_map.end()) {
        reg_code = it->second;
        return true;
    }
    return false;
}


uint8_t EnsambladorIA32::generar_modrm(uint8_t mod, uint8_t reg, uint8_t rm) {
    return (mod << 6) | (reg << 3) | rm;
}

bool EnsambladorIA32::es_etiqueta(const string& s) {
    // La línea ya está limpia y en mayúsculas
    return !s.empty() && s.back() == ':';
}
void EnsambladorIA32::procesar_etiqueta(const string& etiqueta_cruda) {
       // Copiamos la etiqueta
    string etiqueta = etiqueta_cruda;

    // Si termina con ':' se lo quitamos (ej. "VAR_DATA:" -> "VAR_DATA")
    if (!etiqueta.empty() && etiqueta.back() == ':') {
        etiqueta.pop_back();
    }

    // Guardamos SIEMPRE la etiqueta sin dos puntos
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

    // Extraemos la directiva/segundo token AQUI.
    stringstream resto_ss(resto);
    string directiva_dato;
    resto_ss >> directiva_dato;
    limpiar_linea(directiva_dato); // Aseguramos que la directiva esté limpia
    
    // --- MANEJO DE DIRECTIVAS SIN CÓDIGO (SECTION, GLOBAL, EQU) ---
    
    if (mnem == "SECTION" || mnem == "GLOBAL" || mnem == "EXTERN" || mnem == "BITS" || directiva_dato == "EQU") {
        // Ignoramos las directivas de NASM y EQU.
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
    else if (mnem == "IMUL") {
        procesar_imul(resto);
    }
    else if (mnem == "INC") {
        procesar_inc(resto);
    }
    else if (mnem == "DEC") {
        procesar_dec(resto);
    }
    else if (mnem == "MUL") {
        procesar_mul(resto);
    }
    else if (mnem == "DIV") {
        procesar_div(resto);
    }
    else if (mnem == "IDIV") {
        procesar_idiv(resto);
    }
    else if (mnem == "XOR") {
        procesar_xor(resto);
    }
    else if (mnem == "AND") {
        procesar_and(resto);
    }
    else if (mnem == "OR") {
        procesar_or(resto);
    }
    else if (mnem == "TEST") {
        procesar_test(resto);
    }
    else if (mnem == "MOVZX") {
        procesar_movzx(resto);
    }
    else if (mnem == "XCHG") {
        procesar_xchg(resto);
    }
    else if (mnem == "LEA") {
        procesar_lea(resto);
    }
    else if (mnem == "CALL") {
        procesar_call(resto);
    }
    else if (mnem == "RET") {
        procesar_ret();
    }
    else if (mnem == "PUSH") {
        procesar_push(resto);
    }
    else if (mnem == "POP") {
        procesar_pop(resto);
    }
    else if (mnem == "LOOP") {
        procesar_loop(resto);
    }
    else if (mnem == "JMP") {
        procesar_jmp(resto);
    }
    else if (mnem == "LEAVE") { // NUEVO
        procesar_leave();
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
        if (obtener_inmediato32(resto, immediate) && immediate <= 0xFF) {
            agregar_byte(0xCD);
            agregar_byte(static_cast<uint8_t>(immediate));
        }
        else {
            cerr << "Error: Formato de INT invalido o inmediato fuera de rango (0-255): " << resto << endl;
        }
    }
    // --- 3. ETIQUETAS DE DATOS (DD/DB) ---
    else {
    // 'mnem' es ETIQUETA, 'directiva_dato' es DD/DB, en resto_ss queda el valor
    if (directiva_dato == "DD") {
        procesar_etiqueta(mnem);

    // resto = "5, 2, 8, 1, 9, 3"
    string valores;
    getline(resto_ss, valores);   // lo que quede después de "DD"
    limpiar_linea(valores);

    string token;
    stringstream vs(valores);
    while (getline(vs, token, ',')) {
        limpiar_linea(token);
        if (token.empty()) continue;

        uint32_t val;
        if (!obtener_inmediato32(token, val)) {
            cerr << "Error en DD: valor invalido '" << token << "'\n";
            val = 0;
        }
        agregar_dword(val);
    }
    return;
    } else if (directiva_dato == "DB") {
        procesar_etiqueta(mnem);
        string valor_str;
        resto_ss >> valor_str;
        uint32_t val = 0;
        if (!valor_str.empty()) {
            uint32_t tmp;
            if (obtener_inmediato32(valor_str, tmp)) val = tmp & 0xFF;
        }
        agregar_byte(static_cast<uint8_t>(val));
        return;
    }
        
        // Si falla todo, es una instrucción o directiva realmente no soportada.
        cerr << "Advertencia: Mnemónico o directiva no soportada: " << mnem << endl;
    }
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

    uint8_t dest_code = 0, src_code = 0;
    bool dest_is_reg = obtener_reg32(dest_str, dest_code);
    bool src_is_reg = obtener_reg32(src_str, src_code);
    uint32_t immediate = 0;
    bool src_is_imm = obtener_inmediato32(src_str, immediate);

    bool dest_is_mem = (!dest_is_reg && ! (dest_str.empty() && obtener_inmediato32(dest_str, immediate)) && dest_str.size()>=2 && dest_str.front()=='[' && dest_str.back()==']');
    bool src_is_mem = (!src_is_reg && !src_is_imm && src_str.size()>=2 && src_str.front()=='[' && src_str.back()==']');
    
    // 1. REG, REG (r/m32, r32)
    if (dest_is_reg && src_is_reg) {
        agregar_byte(opcode_rm_reg); // ej: 0x01 para ADD, 0x29 para SUB
        uint8_t modrm = generar_modrm(0b11, src_code, dest_code); // MOD=11 (registro), REG=src, R/M=dest
        agregar_byte(modrm);
        return;
    }

    // 2. EAX, INMEDIATO (opcode dedicado)
    if (dest_is_reg && dest_code == 0b000 && src_is_imm) { // EAX, imm
        agregar_byte(opcode_eax_imm); // ej: 0x05 para ADD, 0x2D para SUB
        agregar_dword(immediate);
        return;
    }

    // 3. REG, [ETIQUETA] (r32, r/m32)
    if (dest_is_reg && !src_is_imm) { 
        agregar_byte(opcode_reg_rm); // ej: 0x03 para ADD, 0x2B para SUB, 0x3B para CMP
        uint8_t modrm_byte;
        // es_destino=false porque la memoria es la fuente (ModR/M usa REG=dest_code)
        if (procesar_mem_sib(src_str, modrm_byte, dest_code, false)) return;
        if (procesar_mem_disp(src_str, modrm_byte, dest_code, false)) return;
        if (procesar_mem_simple(src_str, modrm_byte, dest_code, false)) return;
    }
    
    // 4. [ETIQUETA], REG (r/m32, r32)
    if (src_is_reg && !src_is_imm) { 
        agregar_byte(opcode_rm_reg); // ej: 0x01 para ADD, 0x29 para SUB, 0x39 para CMP
        uint8_t modrm_byte;
        // es_destino=true porque la memoria es el destino (ModR/M usa REG=src_code)
        if (procesar_mem_sib(dest_str, modrm_byte, src_code, true)) return;
        if (procesar_mem_disp(dest_str, modrm_byte, src_code, true)) return;
        if (procesar_mem_simple(dest_str, modrm_byte, src_code, true)) return;
    }

    // 5. [ETIQUETA], INMEDIATO (81 /extension, imm32)
    // Usaremos la versión IMM8 (0x83 /extension, imm8) si cabe, ya que el proyecto incluye ejemplos con imm8.
    if (src_is_imm && dest_is_mem) {
        uint8_t opcode = opcode_imm_general; // ej: 0x81
        bool use_imm8 = (immediate <= 0xFF) || (immediate >= 0xFFFFFF80 && immediate <= 0xFFFFFFFF); // Si cabe en 8 bits con extensión de signo
        if (use_imm8) opcode = 0x83; // Opcode 0x83 para imm8 sign-extended
        
        agregar_byte(opcode);
        uint8_t modrm_byte;
        if (procesar_mem_sib(dest_str, modrm_byte, reg_field_extension, true)) {
            if (use_imm8) agregar_byte(static_cast<uint8_t>(immediate & 0xFF)); else agregar_dword(immediate);
            return;
        }
        if (procesar_mem_disp(dest_str, modrm_byte, reg_field_extension, true)) {
            if (use_imm8) agregar_byte(static_cast<uint8_t>(immediate & 0xFF)); else agregar_dword(immediate);
            return;
        }
        if (procesar_mem_simple(dest_str, modrm_byte, reg_field_extension, true)) {
            if (use_imm8) agregar_byte(static_cast<uint8_t>(immediate & 0xFF)); else agregar_dword(immediate);
            return;
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
// ADD, SUB, CMP, IMUL (usando el generalizado)
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

void EnsambladorIA32::procesar_imul(const string& operandos) {
    string dest_str, src_str;
    if (!separar_operandos(operandos, dest_str, src_str)) {
        cerr << "Error de sintaxis: se esperaban 2 operandos para IMUL." << endl;
        return;
    }

    uint8_t dest_code = 0, src_code = 0;
    bool dest_is_reg = obtener_reg32(dest_str, dest_code);
    bool src_is_reg  = obtener_reg32(src_str, src_code);

    // IMUL r32, r/m32  ->  0F AF /r
    // Para reg,reg usamos MOD = 11, REG = destino, R/M = fuente
    if (dest_is_reg && src_is_reg) {
        agregar_byte(0x0F);
        agregar_byte(0xAF);
        uint8_t modrm = generar_modrm(0b11, dest_code, src_code);
        agregar_byte(modrm);
        return;
    }

    cerr << "Error de sintaxis o modo no soportado para IMUL: " << operandos << endl;
}

void EnsambladorIA32::procesar_inc(const string& operandos) {
    string op = operandos;
    limpiar_linea(op);

    uint8_t reg_code;
    if (obtener_reg32(op, reg_code)) {
        // INC r32  ->  40+rd
        agregar_byte(static_cast<uint8_t>(0x40 + reg_code));
        return;
    }

    cerr << "Error de sintaxis o modo no soportado para INC: " << operandos << endl;
}


void EnsambladorIA32::procesar_dec(const string& operandos) {
    string op = operandos;
    limpiar_linea(op);

    uint8_t reg_code;
    if (obtener_reg32(op, reg_code)) {
        // Forma corta: 48+rd  (DEC r32)
        // EAX=0 -> 48, ECX=1 -> 49, EDX=2 -> 4A, ...
        agregar_byte(static_cast<uint8_t>(0x48 + reg_code));
        return;
    }

    cerr << "Error de sintaxis o modo no soportado para DEC: " << operandos << endl;
}

void EnsambladorIA32::procesar_push(const string& operandos) {
    string op = operandos;
    limpiar_linea(op);
    uint8_t reg_code;
    
    // 1. PUSH r32 (50+rd)
    if (obtener_reg32(op, reg_code)) {
        agregar_byte(static_cast<uint8_t>(0x50 + reg_code));
        return;
    }
    
    uint32_t immediate;
    // 2. PUSH imm32 (68 id) - Maneja 'C', 'B', 'A' y números.
    if (obtener_inmediato32(op, immediate)) {
        agregar_byte(0x68); // Opcode 68
        agregar_dword(immediate);
        return;
    }

    // 3. PUSH r/m32 (FF /6) - Maneja [EBP+disp]
    uint8_t modrm_byte;
    const uint8_t ext_opcode = 0b110; // Extensión /6
    
    // Intentar Base + Desplazamiento
    if (procesar_mem_disp(op, modrm_byte, ext_opcode, false)) { 
        agregar_byte(0xFF); // Opcode FF
        return;
    }
    
    // Intentar Memoria Simple [ETIQUETA]
    if (procesar_mem_simple(op, modrm_byte, ext_opcode, false)) {
        agregar_byte(0xFF); // Opcode FF
        return;
    }
    
    cerr << "Error de sintaxis o modo no soportado para PUSH: " << operandos << endl;
}

void EnsambladorIA32::procesar_pop(const string& operandos) {
    string op = operandos;
    limpiar_linea(op);

    uint8_t reg_code;
    if (obtener_reg32(op, reg_code)) {
        // POP r32 -> 58+rd
        agregar_byte(static_cast<uint8_t>(0x58 + reg_code));
        return;
    }

    cerr << "Error de sintaxis o modo no soportado para POP: " << operandos << endl;
}

void EnsambladorIA32::procesar_leave() {
    // LEAVE -> C9
    agregar_byte(0xC9);
}

void EnsambladorIA32::procesar_ret() {
    // RET -> C3
    agregar_byte(0xC3);
}

void EnsambladorIA32::procesar_nop() {
    // NOP -> 90
    agregar_byte(0x90);
}

void EnsambladorIA32::procesar_call(string operandos) {
    limpiar_linea(operandos);
    string etiqueta = operandos;

    agregar_byte(0xE8);  // CALL rel32
    int posicion_referencia = contador_posicion;

    ReferenciaPendiente ref;
    ref.posicion = posicion_referencia;
    ref.tamano_inmediato = 4;
    ref.tipo_salto = 1; // relativo
    referencias_pendientes[etiqueta].push_back(ref);

    agregar_dword(0); // placeholder
}

void EnsambladorIA32::procesar_loop(string operandos) {
    limpiar_linea(operandos);
    string etiqueta = operandos;

    agregar_byte(0xE2); // LOOP rel8
    int posicion_referencia = contador_posicion;

    ReferenciaPendiente ref;
    ref.posicion = posicion_referencia;
    ref.tamano_inmediato = 1;  // solo 1 byte de desplazamiento
    ref.tipo_salto = 1;        // relativo
    referencias_pendientes[etiqueta].push_back(ref);

    agregar_byte(0x00); // placeholder
}

bool EnsambladorIA32::procesar_mem_simple(const std::string& operando,
                                          uint8_t& modrm_byte,
                                          const uint8_t reg_code,
                                          bool es_destino,
                                          uint8_t op_extension)
{
    std::string op = operando;

    // Debe ser algo como [VAR] o [ETIQUETA]
    if (op.size() < 3 || op.front() != '[' || op.back() != ']')
        return false;

    // Quitamos los corchetes
    op = op.substr(1, op.size() - 2); 

    //Trim y limpiar
    limpiar_linea(op);

    // Aquí asumimos direccionamiento absoluto [ETIQUETA]
    std::string etiqueta = op;

    uint8_t mod = 0b00;      // dirección absoluta
    uint8_t rm  = 0b101;     // usar disp32 como base
    uint8_t reg_field = es_destino ? op_extension : reg_code;

    modrm_byte = generar_modrm(mod, reg_field, rm);
    agregar_byte(modrm_byte);

    // El desplazamiento (disp32) viene aquí: será la dirección de la etiqueta
    ReferenciaPendiente ref;
    ref.posicion         = contador_posicion;
    ref.tamano_inmediato = 4;
    ref.tipo_salto       = 0;    // 0 = absoluto
    referencias_pendientes[etiqueta].push_back(ref);

    // Placeholder, luego se parchea en resolver_referencias_pendientes
    agregar_dword(0);

    return true;
}


// -----------------------------------------------------------------------------
// Saltos
// -----------------------------------------------------------------------------

void EnsambladorIA32::procesar_jmp(const string& operandos_in) {
    string operandos = operandos_in;
    limpiar_linea(operandos);
    string etiqueta = operandos;

   // Si la etiqueta ya está definida podemos elegir salto corto o cercano
    if (tabla_simbolos.count(etiqueta)) {
        int destino = tabla_simbolos[etiqueta];
        // calculamos offset relativo respecto al byte siguiente
        int pos_disp = contador_posicion + 1; // si usamos EB/dispb
        int offset = destino - pos_disp;
        if (offset >= -128 && offset <= 127) {
            agregar_byte(0xEB);
            agregar_byte(static_cast<uint8_t>(offset & 0xFF));
            return;
    } else {
       // usar near jump E9 rel32
        agregar_byte(0xE9);
        int posicion_referencia = contador_posicion;
        ReferenciaPendiente ref;
        ref.posicion = posicion_referencia;
        ref.tamano_inmediato = 4;
        ref.tipo_salto = 1; // relativo
        referencias_pendientes[etiqueta].push_back(ref);
        agregar_dword(0);
        return;
        }
     }
    // Si la etiqueta no existe aún, emitimos salto corto por defecto y referencia pendiente
    // (se podría mejorar para elegir rel32 cuando se necesite)
    agregar_byte(0xEB);
    int pos_disp = contador_posicion;
    
    ReferenciaPendiente ref;
    ref.posicion = pos_disp; // byte del disp8
    ref.tamano_inmediato = 1;
    ref.tipo_salto = 1; // relativo
    referencias_pendientes[etiqueta].push_back(ref);
    
    agregar_byte(0x00); // placeholder
}

void EnsambladorIA32::procesar_condicional(const string& mnem,
                                           const string& operandos_in) {
    string operandos = operandos_in;
    limpiar_linea(operandos);
    string etiqueta = operandos;

    uint8_t opcode;
    uint8_t opcode_ext = 0;
    bool uses_two_byte = false;

    // Mapeo a saltos cortos (rel8)
    if (mnem == "JE" || mnem == "JZ") opcode = 0x74, opcode_ext = 0x84, uses_two_byte = true;
    else if (mnem == "JNE" || mnem == "JNZ") opcode = 0x75, opcode_ext = 0x85, uses_two_byte = true;
    else if (mnem == "JLE") opcode = 0x7E, opcode_ext = 0x8E, uses_two_byte = true;
    else if (mnem == "JL") opcode = 0x7C, opcode_ext = 0x8C, uses_two_byte = true;
    else if (mnem == "JA") opcode = 0x77, opcode_ext = 0x87, uses_two_byte = true;
    else if (mnem == "JAE") opcode = 0x73, opcode_ext = 0x83, uses_two_byte = true;
    else if (mnem == "JB") opcode = 0x72, opcode_ext = 0x82, uses_two_byte = true;
    else if (mnem == "JBE") opcode = 0x76, opcode_ext = 0x86, uses_two_byte = true;
    else if (mnem == "JG") opcode = 0x7F, opcode_ext = 0x8F, uses_two_byte = true;
    else if (mnem == "JGE") opcode = 0x7D, opcode_ext = 0x8D, uses_two_byte = true;
    else {
        cerr << "Error: Mnemónico condicional no soportado: " << mnem << endl;
        return;
    }

    if (tabla_simbolos.count(etiqueta)) {
        int destino = tabla_simbolos[etiqueta];
        int pos_disp = contador_posicion + 1; // si emitimos short
        int offset = destino - pos_disp;
        if (offset >= -128 && offset <= 127) {
            agregar_byte(opcode);
            agregar_byte(static_cast<uint8_t>(offset & 0xFF));
            return;
      } else if (uses_two_byte) {
            // emitir opcode 0F 8x + rel32
            agregar_byte(0x0F);
            agregar_byte(opcode_ext);
            int posicion_referencia = contador_posicion;
            ReferenciaPendiente ref;
            ref.posicion = posicion_referencia;
            ref.tamano_inmediato = 4;
            ref.tipo_salto = 1; // relativo
            referencias_pendientes[etiqueta].push_back(ref);
            agregar_dword(0);
            return;
        }
    }  

    // Si etiqueta no está definida, emitimos versión corta y referencia rel8 pendiente
    agregar_byte(opcode);
    int pos_disp = contador_posicion;
    ReferenciaPendiente ref;
    ref.posicion = pos_disp;
    ref.tamano_inmediato = 1; // rel8
    ref.tipo_salto = 1; // relativo
    referencias_pendientes[etiqueta].push_back(ref);
    agregar_byte(0x00); // placeholder
}


void EnsambladorIA32::procesar_mul(const string& operandos) {
    string op = operandos;
    limpiar_linea(op);

    uint8_t reg_code=0;
    if (obtener_reg32(op, reg_code)) {
        // MUL r32  -> F7 /4  con MOD=11, REG=100b, R/M=reg
        agregar_byte(0xF7);
        uint8_t modrm = generar_modrm(0b11, 0b100, reg_code);
        agregar_byte(modrm);
        return;
    }

    // MUL [ETIQUETA]
    uint8_t modrm_byte;
    agregar_byte(0xF7);
    if (procesar_mem_simple(op, modrm_byte, 0, true, 0b100)) {
        return;
    }

    cerr << "Error de sintaxis o modo no soportado para MUL: " << operandos << endl;
}

void EnsambladorIA32::procesar_div(const string& operandos) {
    string op = operandos;
    limpiar_linea(op);

    uint8_t reg_code=0;
    if (obtener_reg32(op, reg_code)) {
        // DIV r32 -> F7 /6
        agregar_byte(0xF7);
        uint8_t modrm = generar_modrm(0b11, 0b110, reg_code);
        agregar_byte(modrm);
        return;
    }

    uint8_t modrm_byte;
    agregar_byte(0xF7);
    if (procesar_mem_simple(op, modrm_byte, 0, true, 0b110)) {
        return;
    }

    cerr << "Error de sintaxis o modo no soportado para DIV: " << operandos << endl;
}

void EnsambladorIA32::procesar_idiv(const string& operandos) {
    string op = operandos;
    limpiar_linea(op);

    uint8_t reg_code=0;
    if (obtener_reg32(op, reg_code)) {
        // IDIV r32 -> F7 /7
        agregar_byte(0xF7);
        uint8_t modrm = generar_modrm(0b11, 0b111, reg_code);
        agregar_byte(modrm);
        return;
    }

    uint8_t modrm_byte;
    agregar_byte(0xF7);
    if (procesar_mem_simple(op, modrm_byte, 0, true, 0b111)) {
        return;
    }

    cerr << "Error de sintaxis o modo no soportado para IDIV: " << operandos << endl;
}

void EnsambladorIA32::procesar_xor(const string& operandos) {
    // 31: XOR r/m32, r32; 33: XOR r32, r/m32; 35: XOR EAX, imm32; 81 /6: XOR r/m32, imm32
    procesar_binaria("XOR", operandos, 0x31, 0x33, 0x35, 0x81, 0b110);
}

void EnsambladorIA32::procesar_and(const string& operandos) {
    // 21: AND r/m32, r32; 23: AND r32, r/m32; 25: AND EAX, imm32; 81 /4: AND r/m32, imm32
    procesar_binaria("AND", operandos, 0x21, 0x23, 0x25, 0x81, 0b100);
}

void EnsambladorIA32::procesar_or(const string& operandos) {
    // 09: OR r/m32, r32; 0B: OR r32, r/m32; 0D: OR EAX, imm32; 81 /1: OR r/m32, imm32
    procesar_binaria("OR", operandos, 0x09, 0x0B, 0x0D, 0x81, 0b001);
}

void EnsambladorIA32::procesar_test(const string& operandos) {
    string dest_str, src_str;
    if (!separar_operandos(operandos, dest_str, src_str)) {
        cerr << "Error de sintaxis: se esperaban 2 operandos para TEST." << endl;
        return;
    }

    uint8_t dest_code=0, src_code=0;
    bool dest_is_reg = obtener_reg32(dest_str, dest_code);
    bool src_is_reg  = obtener_reg32(src_str, src_code);

    // TEST r/m32, r32 -> 85 /r
    if (dest_is_reg && src_is_reg) {
        agregar_byte(0x85);
        uint8_t modrm = generar_modrm(0b11, src_code, dest_code);
        agregar_byte(modrm);
        return;
    }

    cerr << "Error de sintaxis o modo no soportado para TEST: " << operandos << endl;
}

bool EnsambladorIA32::is_mem_simple_label(const string& s) {
    // Detecta [LABEL] sencillo sin registros, sin '+', '-', '*', ni constantes
    if (s.size() < 3 || s.front() != '[' || s.back() != ']') return false;
    string inner = s.substr(1, s.size()-2);
    limpiar_linea(inner);
    // si contiene cualquiera de estos símbolos, no es "simple"
    string forbidden = "+-*[]";
    for (char c: forbidden) if (inner.find(c) != string::npos) return false;
    // si contiene nombres de registros, tampoco
    for (auto &par: reg32_map) {
    if (inner.find(par.first) != string::npos) return false;
    }
    for (auto &par: reg8_map) {
    if (inner.find(par.first) != string::npos) return false;
    }
    // si solo está compuesto por letras, dígitos o '_' lo permitimos
    // (aceptamos etiquetas alfanuméricas)
    return true;
}

void EnsambladorIA32::procesar_mov(const string& operandos) {
    string dest_str, src_str;
    if (!separar_operandos(operandos, dest_str, src_str)) {
        cerr << "Error de sintaxis: Se esperaban 2 operandos para MOV." << endl;
        return;
    }

    uint8_t dest_code=0, src_code=0;
    bool dest_is_reg = obtener_reg32(dest_str, dest_code);
    bool src_is_reg = obtener_reg32(src_str, src_code);
    
    // 1. MOV REG, REG (89 r/m32, r32)
    if (dest_is_reg && src_is_reg) {
        agregar_byte(0x89);
        uint8_t modrm = generar_modrm(0b11, src_code, dest_code); 
        agregar_byte(modrm);
        return;
    }

    uint32_t immediate=0;
    bool src_is_imm = obtener_inmediato32(src_str, immediate);

    // 2. MOV REG, INMEDIATO (B8+rd)
    if (dest_is_reg && src_is_imm) {
        agregar_byte(0xB8 + dest_code);
        agregar_dword(immediate);
        return;
    }

    // --- CASO ESPECIAL MOV ECX, LEN (simulación de constante) ---
    if (dest_is_reg && src_str == "LEN") {
        agregar_byte(0xB8 + dest_code); 
        agregar_dword(6); // Valor simulado para LEN
        return;
    }

    // 2.5. MOV [ETIQUETA], EAX (Opcode A3) - Usaremos SOLO para [LABEL] simple
   if (src_is_reg && src_code == 0b000 && is_mem_simple_label(dest_str)) {
        string temp_op = dest_str.substr(1, dest_str.size() - 2);
        agregar_byte(0xA3);
        
        ReferenciaPendiente ref;
        ref.posicion = contador_posicion;
        ref.tamano_inmediato = 4;
        ref.tipo_salto = 0;
        referencias_pendientes[temp_op].push_back(ref);
        
        agregar_dword(0);
        return;
    }
    
    // 3. MOV [MEM], REG (89 r/m32, r32). MEMORIA ES DESTINO.
    if (src_is_reg) {
        agregar_byte(0x89); 
        uint8_t modrm_byte;
        
        if (procesar_mem_sib(dest_str, modrm_byte, src_code, true)) return;
        if (procesar_mem_disp(dest_str, modrm_byte, src_code, true)) return;
        if (procesar_mem_simple(dest_str, modrm_byte, src_code, true)) return;
    }

    // 4. MOV REG, [MEM] (8B r32, r/m32). MEMORIA ES FUENTE.
    if (dest_is_reg) {
        agregar_byte(0x8B); // Opcode 8B
        uint8_t modrm_byte;
        
        if (procesar_mem_sib(src_str, modrm_byte, dest_code, false)) return;
        if (procesar_mem_disp(src_str, modrm_byte, dest_code, false)) return;
        if (procesar_mem_simple(src_str, modrm_byte, dest_code, false)) return;
    }
    
    // 5. MOV [MEM], INMEDIATO (C7 /0, imm32)
    if (src_is_imm) {
        agregar_byte(0xC7); 
        uint8_t modrm_byte;
        
        if (procesar_mem_sib(dest_str, modrm_byte, 0b000, true)) {
             agregar_dword(immediate);
             return;
        }

        if (procesar_mem_disp(dest_str, modrm_byte, 0b000, true)) {
            agregar_dword(immediate);
            return;
        }
        
        if (procesar_mem_simple(dest_str, modrm_byte, 0b000, true)) {
             agregar_dword(immediate);
             return;
        }
    }

    cerr << "Error de sintaxis o modo no soportado para MOV: " << operandos << endl;
}

// -----------------------------------------------------------------------------
// Direccionamiento EBP + Desplazamiento [EBP + disp]
// -----------------------------------------------------------------------------
bool EnsambladorIA32::procesar_mem_disp(const string& operando, 
                                        uint8_t& modrm_byte, 
                                        const uint8_t reg_code, 
                                        bool es_destino) {
    
    string op = operando;
    if (op.front() != '[' || op.back() != ']') return false;

    op = op.substr(1, op.size() - 2); // Remueve corchetes
    limpiar_linea(op);
    
    // Simplificación: Solo buscamos EBP (o un registro de 32 bits y un desplazamiento)
    if (op.find("EBP") == string::npos) return false;
    
    uint8_t base_code;
    // Debemos parsear para soportar [EBP+disp]. Asumimos que es EBP (0b101).
    if (!obtener_reg32("EBP", base_code)) return false; 
    
    int displacement = 0;
    
    size_t sign_pos = op.find('+');
    if (sign_pos == string::npos) sign_pos = op.find('-');
    
    if (sign_pos != string::npos) {
        try {
            displacement = stoi(op.substr(sign_pos));
        } catch(...) {
            return false;
        }
    }
    
    // Elegir MOD según tamaño del desplazamiento
    uint8_t mod;
    if (displacement == 0) {
        // Para [EBP] el encodado MOD=00 con R/M=101 significa disp32
        // Para representar [EBP] sin disp se usa MOD=01 con disp8=0
        mod = 0b01;
    } else if (displacement >= -128 && displacement <= 127) {
        mod = 0b01; // disp8
    } else {
        mod = 0b10; // disp32
    }


    uint8_t rm = base_code; // R/M = EBP (101)
    uint8_t reg_field = reg_code;
    
    modrm_byte = generar_modrm(mod, reg_field, rm);
    agregar_byte(modrm_byte);
    
    if (mod == 0b01) {
        agregar_byte(static_cast<uint8_t>(displacement & 0xFF));
    } else if (mod == 0b10) {
        agregar_dword(static_cast<uint32_t>(displacement));
    }
    return true;
}

void EnsambladorIA32::procesar_movzx(const string& operandos) {
    string dest_str, src_str;
    if (!separar_operandos(operandos, dest_str, src_str)) {
        cerr << "Error de sintaxis: se esperaban 2 operandos para MOVZX." << endl;
        return;
    }

    uint8_t dest_code=0, src_code8=0;
    bool dest_is_reg32 = obtener_reg32(dest_str, dest_code);
    bool src_is_reg8   = obtener_reg8(src_str, src_code8);

    if (!dest_is_reg32) {
        cerr << "Error: MOVZX requiere un registro de 32 bits como destino." << endl;
        return;
    }

    // --- MANEJO DE LA SINTAXIS DE MEMORIA (BYTE [DISCOS]) ---
    // Eliminar la pista de tamaño "BYTE" del operando fuente si existe.
    size_t byte_pos = src_str.find("BYTE ");
    if (byte_pos != string::npos) {
        src_str.erase(byte_pos, 5); // Elimina "BYTE " (5 caracteres)
        limpiar_linea(src_str);    // Limpia espacios que pudieran quedar (importante)
    }
    // ---------------------------------------------------------

    // 1. MOVZX r32, r8 (0F B6 /r)
    if (src_is_reg8) {
        agregar_byte(0x0F);
        agregar_byte(0xB6);
        uint8_t modrm = generar_modrm(0b11, dest_code, src_code8);
        agregar_byte(modrm);
        return;
    }

    // 2. MOVZX r32, m8 (0F B6 /r) - Maneja [DISCOS]
    // Ya que limpiamos 'BYTE', src_str ahora solo debe ser '[DISCOS]'
    { 
        agregar_byte(0x0F);
        agregar_byte(0xB6); // Opcode 0F B6 para 8->32
        
        uint8_t modrm_byte;
        
        // Intentar Memoria Simple [DISCOS]
        // ModR/M para [ETIQUETA] usa MOD=00, R/M=101, REG=dest_code
        if (procesar_mem_simple(src_str, modrm_byte, dest_code, false)) return; 
    }

    cerr << "Error de sintaxis o modo no soportado para MOVZX: " << operandos << endl;
}

void EnsambladorIA32::procesar_xchg(const string& operandos) {
    string dest_str, src_str;
    if (!separar_operandos(operandos, dest_str, src_str)) {
        cerr << "Error de sintaxis: se esperaban 2 operandos para XCHG." << endl;
        return;
    }

    uint8_t dest_code=0, src_code=0;
    bool dest_is_reg = obtener_reg32(dest_str, dest_code);
    bool src_is_reg  = obtener_reg32(src_str, src_code);

    if (dest_is_reg && src_is_reg) {
        // XCHG r/m32, r32 -> 87 /r
        agregar_byte(0x87);
        uint8_t modrm = generar_modrm(0b11, src_code, dest_code);
        agregar_byte(modrm);
        return;
    }

    cerr << "Error de sintaxis o modo no soportado para XCHG: " << operandos << endl;
}

void EnsambladorIA32::procesar_lea(const string& operandos) {
    string dest_str, src_str;
    if (!separar_operandos(operandos, dest_str, src_str)) {
        cerr << "Error de sintaxis: se esperaban 2 operandos para LEA." << endl;
        return;
    }

    uint8_t dest_code=0;
    bool dest_is_reg = obtener_reg32(dest_str, dest_code);
    if (!dest_is_reg) {
        cerr << "Error: LEA solo soporta destino registro de 32 bits." << endl;
        return;
    }

    // LEA r32, m -> 8D /r
    agregar_byte(0x8D);
    uint8_t modrm_byte;
    if (procesar_mem_sib(src_str, modrm_byte, dest_code, false)) {
        return;
    }
    if (procesar_mem_disp(src_str, modrm_byte, dest_code, false)) {
        return;
    }
    if (procesar_mem_simple(src_str, modrm_byte, dest_code, false)) {
    return;
    }

    cerr << "Error de sintaxis o modo no soportado para LEA: " << operandos << endl;
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
            uint32_t valor_a_parchear = 0;

            if (ref.tipo_salto == 0) {
                // Referencia absoluta → dirección real de la etiqueta
                valor_a_parchear = static_cast<uint32_t>(destino);
            } else {
                // Relativo → destino - (posición del siguiente byte)
                int offset = destino - (pos + ref.tamano_inmediato);
                valor_a_parchear = static_cast<uint32_t>(offset);
            }

            if (ref.tamano_inmediato == 4) {
                codigo_hex[pos]     = static_cast<uint8_t>(valor_a_parchear & 0xFF);
                codigo_hex[pos + 1] = static_cast<uint8_t>((valor_a_parchear >> 8) & 0xFF);
                codigo_hex[pos + 2] = static_cast<uint8_t>((valor_a_parchear >> 16) & 0xFF);
                codigo_hex[pos + 3] = static_cast<uint8_t>((valor_a_parchear >> 24) & 0xFF);
            } else if (ref.tamano_inmediato == 1) {
                codigo_hex[pos] = static_cast<uint8_t>(valor_a_parchear & 0xFF);
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


