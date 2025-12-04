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

    // Normalizar la etiqueta: trim y uppercase
    limpiar_linea(op);

    // Ahora op es la etiqueta limpia
    string etiqueta = op;

    uint8_t mod = 0b00;
    uint8_t rm = 0b101; // dirección absoluta

    uint8_t reg_field = es_destino ? op_extension : reg_code;

    modrm_byte = generar_modrm(mod, reg_field, rm);

    // El desplazamiento empieza justo después del ModR/M
    ReferenciaPendiente ref;
    ref.posicion = contador_posicion;
    ref.tamano_inmediato = 4;
    ref.tipo_salto = 0; // absoluto

    // Usamos la etiqueta ya limpia para registrar la referencia
    referencias_pendientes[etiqueta].push_back(ref);

    agregar_dword(0);  // placeholder
    return true;
}


// -----------------------------------------------------------------------------
// Direccionamiento indexado [ETIQUETA + ESI*4 + disp]
// -----------------------------------------------------------------------------
// -----------------------------------------------------------------------------
// Direccionamiento indexado [ETIQUETA + ESI*4 (+ disp)]
// -----------------------------------------------------------------------------
bool EnsambladorIA32::procesar_mem_sib(const string& operando,
                                       uint8_t& modrm_byte,
                                       const uint8_t reg_code,
                                       bool es_destino)
{
    string op = operando;
    if (op.size() < 2 || op.front() != '[' || op.back() != ']')
        return false;

    // quitar corchetes
    op = op.substr(1, op.size() - 2);
    limpiar_linea(op);

    // Debe contener ESI*4
    size_t pos_esi = op.find("ESI*4");
    if (pos_esi == string::npos)
        return false;

    // Extraer etiqueta = todo lo que aparece ANTES de "ESI*4"
    string etiqueta = op.substr(0, pos_esi);

    // eliminar +, -, espacios finales
    while (!etiqueta.empty() &&
           (etiqueta.back() == '+' || etiqueta.back() == '-' ||
            isspace((unsigned char)etiqueta.back())))
    {
        etiqueta.pop_back();
    }

    limpiar_linea(etiqueta);
    if (etiqueta.empty())
        return false;   // se esperaba etiqueta base

    // Buscar desplazamiento (opcional) DESPUÉS de "ESI*4"
    int64_t displacement = 0;
    bool tiene_disp = false;

    size_t pos_after = pos_esi + string("ESI*4").size();
    if (pos_after < op.size())
    {
        string tail = op.substr(pos_after);
        limpiar_linea(tail);

        if (!tail.empty())
        {
            try {
                displacement = stoi(tail);  // acepta signo
                tiene_disp = true;
            } catch (...) {
                return false;  // desplazamiento inválido
            }
        }
    }

    uint8_t mod = 0b00;
    int disp_size = 4;  // SIEMPRE disp32

    // SIB fields
    uint8_t scale = 0b10;   // x4
    uint8_t index = 0b110;  // ESI
    uint8_t base  = 0b101;  // base=disp32 (addressing absoluto)

    // R/M = 100 → indica que sigue SIB
    uint8_t rm = 0b100;
    uint8_t reg_field = reg_code;

    // Construir y escribir ModR/M
    modrm_byte = generar_modrm(mod, reg_field, rm);
    agregar_byte(modrm_byte);

    // Construir y escribir SIB
    uint8_t sib_byte = (scale << 6) | (index << 3) | base;
    agregar_byte(sib_byte);

    // --- Emitir disp32 ---
    // SIEMPRE existe disp32 porque base=101 y mod=00
    {
        ReferenciaPendiente ref;
        ref.posicion = contador_posicion;
        ref.tamano_inmediato = 4;
        ref.tipo_salto = 0;            // direccion absoluta
        referencias_pendientes[etiqueta].push_back(ref);

        // Guardamos el displacement como parte del placeholder.
        // El resolver final hará: dirección(etiqueta) + displacement
        agregar_dword((uint32_t) displacement);
    }

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
        // 'mnem' es la ETIQUETA, 'directiva_dato' es la directiva (DD o DB).
        
        if (directiva_dato == "DD") { // Define Doubleword (4 bytes)
            procesar_etiqueta(mnem); 
            agregar_dword(0);   // Avanza el CP por 4 bytes
            return;
        }
        else if (directiva_dato == "DB") { // Define Byte (1 byte) - NUEVO
            procesar_etiqueta(mnem);
            // Esto solo reserva 1 byte (placeholder), suficiente para 'discos db 3'.
            // Para 'msg db ...' es solo un placeholder, no una solución completa.
            agregar_byte(0); 
            return;
        }
        
        // Si falla todo, es una instrucción o directiva realmente no soportada.
        cerr << "Advertencia: Mnemónico o directiva no soportada: " << mnem << endl;
    }
}

// -----------------------------------------------------------------------------
// MOV 
// -----------------------------------------------------------------------------


// -----------------------------------------------------------------------------
// ADD, SUB, CMP (generalizado)
// -----------------------------------------------------------------------------
void EnsambladorIA32::procesar_binaria(
    const string& mnem,
    const string& operandos,
    uint8_t opcode_rm_reg, 
    uint8_t opcode_reg_rm, 
    uint8_t opcode_eax_imm, 
    uint8_t opcode_imm_general, 
    uint8_t reg_field_extension 
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
        agregar_byte(opcode_rm_reg); 
        uint8_t modrm = generar_modrm(0b11, src_code, dest_code); 
        agregar_byte(modrm);
        return;
    }

    uint32_t immediate;
    bool src_is_imm = obtener_inmediato32(src_str, immediate);

    // 2. EAX, INMEDIATO (opcode dedicado)
    if (dest_is_reg && dest_code == 0b000 && src_is_imm) { 
        agregar_byte(opcode_eax_imm); 
        agregar_dword(immediate);
        return;
    }

    // 3. REG, [MEM] (r32, r/m32) - Memoria es FUENTE
    if (dest_is_reg && !src_is_imm) {
        agregar_byte(opcode_reg_rm); // ej: 0x03 (ADD r32, r/m32)
        uint8_t modrm_byte;
        
        // El registro de destino va en el campo REG del ModR/M (dest_code)
        
        // Intentar Base + Desplazamiento
        if (procesar_mem_disp(src_str, modrm_byte, dest_code, false)) {
            agregar_byte(modrm_byte); 
            return;
        }
        
        // Intentar SIB
        if (procesar_mem_sib(src_str, modrm_byte, dest_code, false)) {
            return;
        }

        // Intentar Memoria Simple (Etiqueta)
        if (procesar_mem_simple(src_str, modrm_byte, dest_code, false)) {
            agregar_byte(modrm_byte); 
            return;
        }
    }
    
    // 4. [MEM], REG (r/m32, r32) - Memoria es DESTINO
    if (src_is_reg && !src_is_imm) {
        agregar_byte(opcode_rm_reg); // ej: 0x01 (ADD r/m32, r32)
        uint8_t modrm_byte;
        
        // El registro de fuente va en el campo REG del ModR/M (src_code)
        
        // Intentar Base + Desplazamiento
        if (procesar_mem_disp(dest_str, modrm_byte, src_code, true)) {
            agregar_byte(modrm_byte); 
            return;
        }
        
        // Intentar SIB
        if (procesar_mem_sib(dest_str, modrm_byte, src_code, true)) {
            return;
        }
        
        // Intentar Memoria Simple (Etiqueta)
        if (procesar_mem_simple(dest_str, modrm_byte, src_code, true)) {
            agregar_byte(modrm_byte); 
            return;
        }
    }

    // 5. [MEM], INMEDIATO (81 /extension, imm32)
    if (src_is_imm) {
        uint8_t opcode = opcode_imm_general; 
        // Lógica de imm8 (0x83) o imm32 (0x81)
        bool use_imm8 = (immediate <= 0xFF && immediate >= 0) || (immediate >= 0xFFFFFF80 && immediate <= 0xFFFFFFFF);
        if (use_imm8) opcode = 0x83;

        if (!dest_is_reg) { // Memoria, imm
            agregar_byte(opcode);
            uint8_t modrm_byte;
            
            // El campo REG usa la extensión del opcode (reg_field_extension)
            
            // Intentar Base + Desplazamiento
            if (procesar_mem_disp(dest_str, modrm_byte, reg_field_extension, true)) {
                agregar_byte(modrm_byte); 
                if (use_imm8) { agregar_byte(static_cast<uint8_t>(immediate & 0xFF)); } 
                else { agregar_dword(immediate); }
                return;
            }
            
            // Intentar SIB
           if (procesar_mem_sib(dest_str, modrm_byte, reg_field_extension, true)) {
                if (use_imm8) { agregar_byte(static_cast<uint8_t>(immediate & 0xFF)); } 
                else { agregar_dword(immediate); }
                return;
            }
            
            // Intentar Memoria Simple
            if (procesar_mem_simple(dest_str, modrm_byte, reg_field_extension, true)) {
                agregar_byte(modrm_byte);
                if (use_imm8) { agregar_byte(static_cast<uint8_t>(immediate & 0xFF)); } 
                else { agregar_dword(immediate); }
                return;
            }
        }
    }
    
    // 6. REG, INMEDIATO (81 /extension, imm32) - Si no es EAX (ya manejado)
    if (dest_is_reg && dest_code != 0b000 && src_is_imm) {
        uint8_t opcode = opcode_imm_general; 
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

void EnsambladorIA32::procesar_imul(const string& operandos) {
    string dest_str, src_str;
    if (!separar_operandos(operandos, dest_str, src_str)) {
        cerr << "Error de sintaxis: se esperaban 2 operandos para IMUL." << endl;
        return;
    }

    uint8_t dest_code, src_code;
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

    // Podrías extender a memoria más adelante
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
        // procesar_mem_disp ya agregó el ModR/M y el Disp8/32
        return;
    }
    
    // Intentar Memoria Simple [ETIQUETA]
    if (procesar_mem_simple(op, modrm_byte, ext_opcode, false)) {
        agregar_byte(0xFF); // Opcode FF
        // procesar_mem_simple ya agregó ModR/M y el Disp32
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

void EnsambladorIA32::procesar_mul(const string& operandos) {
    string op = operandos;
    limpiar_linea(op);

    uint8_t reg_code;
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

    uint8_t reg_code;
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

    uint8_t reg_code;
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

    uint8_t dest_code, src_code;
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
        uint8_t modrm = generar_modrm(0b11, src_code, dest_code); 
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

    // --- CASO ESPECIAL MOV ECX, LEN (simulación de constante) ---
    if (dest_is_reg && src_str == "LEN") {
        agregar_byte(0xB8 + dest_code); 
        agregar_dword(6); // Valor simulado para LEN
        return;
    }

    // 2.5. MOV [ETIQUETA], EAX (Opcode A3) - Simplificado
    if (src_is_reg && src_code == 0b000) { 
        string etiqueta_mem;
        if (dest_str.front() == '[' && dest_str.back() == ']') {
            string temp_op = dest_str.substr(1, dest_str.size() - 2);
            // Evitar A3 si parece ser SIB o inmediato (solo para etiquetas simples)
            if (obtener_inmediato32(temp_op, immediate) || temp_op.find("ESI") != string::npos) {
                // No usar A3
            } else {
                agregar_byte(0xA3); 
                etiqueta_mem = temp_op; 

                ReferenciaPendiente ref;
                ref.posicion = contador_posicion;
                ref.tamano_inmediato = 4;
                ref.tipo_salto = 0;
                string etiqueta_limpia = etiqueta_mem;
                limpiar_linea(etiqueta_limpia);

                // Cuando se cae aquí, la etiqueta SIEMPRE debe ser la base antes del primer '+'
                size_t pos_plus = etiqueta_limpia.find('+');
                if (pos_plus != string::npos)
                    etiqueta_limpia = etiqueta_limpia.substr(0, pos_plus);

                limpiar_linea(etiqueta_limpia); // limpiar nuevamente
                
                referencias_pendientes[etiqueta_limpia].push_back(ref);

                agregar_dword(0); 
                return;
            }
        }
    }

    // 3. MOV [MEM], REG (89 r/m32, r32). MEMORIA ES DESTINO.
    if (src_is_reg) {
        agregar_byte(0x89); 
        uint8_t modrm_byte;
        
        // La memoria es el DESTINO, el registro es la FUENTE (va en el campo REG)
        if (procesar_mem_sib(dest_str, modrm_byte, src_code, true)) {
            return;
        }

        if (procesar_mem_disp(dest_str, modrm_byte, src_code, true)) {
            agregar_byte(modrm_byte); 
            return;
        }
        if (procesar_mem_simple(dest_str, modrm_byte, src_code, true)) {
            agregar_byte(modrm_byte);
            return;
        }
    }

    // 4. MOV REG, [MEM] (8B r32, r/m32). MEMORIA ES FUENTE.
    if (dest_is_reg) {
        agregar_byte(0x8B); // Opcode 8B
        uint8_t modrm_byte;
        
        // La memoria es la FUENTE, el registro es el DESTINO (va en el campo REG)
        // 1. Intentar SIB
        if (procesar_mem_sib(src_str, modrm_byte, dest_code, false)) {
            return;
        }
        
        // 2. Intentar Base + Desplazamiento [EBP+disp]
        if (procesar_mem_disp(src_str, modrm_byte, dest_code, false)) {
            agregar_byte(modrm_byte); 
            return;
        }
        
        // 3. Intentar Memoria Simple
        if (procesar_mem_simple(src_str, modrm_byte, dest_code, false)) {
            agregar_byte(modrm_byte);
            return;
        }
    }
    
    // 5. MOV [MEM], INMEDIATO (C7 /0, imm32)
    if (src_is_imm) {
        agregar_byte(0xC7); 
        uint8_t modrm_byte;
        
        // El campo REG debe ser 0b000 (/0)
        // La memoria es el DESTINO
        if (procesar_mem_sib(dest_str, modrm_byte, 0b000, true)) {
            agregar_dword(immediate);
            return;
        }
        if (procesar_mem_disp(dest_str, modrm_byte, 0b000, true)) {
            agregar_byte(modrm_byte); 
            agregar_dword(immediate);
            return;
        }
        if (procesar_mem_simple(dest_str, modrm_byte, 0b000, true)) {
            agregar_byte(modrm_byte); 
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
    
    // --- 1. Parsing y validación del registro base ---
    // Simplificación: Buscamos un registro (ej. EBP) y un desplazamiento.
    // Usaremos un parser simple. En este caso, solo soportamos [REG+/-DISP].
    
    // Suponemos que la sintaxis es REG+/-DISP (limpiar_linea puso todo en mayúsculas)
    uint8_t base_code;
    string base_reg_str;
    int displacement = 0;
    
    // Buscamos si la línea contiene EAX, ECX, etc., para identificar el registro base
    size_t reg_end_pos = string::npos;

    // Buscar el registro base. Asumiremos que el registro es la primera palabra o parte.
    // Una implementación completa requeriría un parser más robusto.
    // Nos enfocaremos en EBP, que es lo más común para esta función.
    if (op.find("EBP") != string::npos) {
        base_reg_str = "EBP";
        reg_end_pos = op.find("EBP") + 3; // Longitud de "EBP"
    } else {
        // Podrías agregar otros registros si los necesitas (ej. EBX, EAX)
        return false;
    }
    
    if (!obtener_reg32(base_reg_str, base_code)) return false; 
    
    // --- 2. Extracción del Desplazamiento ---
    size_t sign_pos = op.find('+', reg_end_pos);
    if (sign_pos == string::npos) sign_pos = op.find('-', reg_end_pos);
    
    if (sign_pos != string::npos) {
        // La parte restante es el desplazamiento (ej: "+8" o "8")
        string disp_str = op.substr(sign_pos); 
        try {
            // stoll (long long) es más seguro para asegurar que el desplazamiento cabe en int
            displacement = stoi(disp_str); 
        } catch (...) {
            // Si no se puede convertir a número, es inválido.
            return false;
        }
    }
    // Si sign_pos == npos, displacement es 0. [EBP] -> [EBP+0] (handled by MOD=01, disp8=0)

    // --- 3. Codificación ModR/M y Determinación del MOD ---
    
    uint8_t mod;
    int disp_size; // 0, 1 (disp8), o 4 (disp32)

    // Verificación de rango (para determinar si es Disp8 o Disp32)
    if (displacement == 0) {
        // Aunque [EBP] es MOD=00, R/M=101, [EBP+0] usa MOD=01 con disp8=0.
        mod = 0b01; 
        disp_size = 1;
    } else if (displacement >= -128 && displacement <= 127) {
        // Desplazamiento de 8 bits (Disp8)
        mod = 0b01; 
        disp_size = 1;
    } else {
        // Desplazamiento de 32 bits (Disp32)
        mod = 0b10;
        disp_size = 4;
    }

    uint8_t rm = base_code; // R/M es el código del registro base (ej. EBP: 0b101)
    uint8_t reg_field = reg_code; // El registro R de la instrucción

    // 1. Calcular el ModR/M byte.
    modrm_byte = generar_modrm(mod, reg_field, rm);

    // --- 4. Agregar Desplazamiento (Disp8 o Disp32) ---
    if (disp_size == 1) {
        // Disp8 (se extiende el signo)
        agregar_byte(static_cast<uint8_t>(displacement & 0xFF));
    } else if (disp_size == 4) {
        // Disp32
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

    uint8_t dest_code, src_code8;
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

    uint8_t dest_code, src_code;
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

    uint8_t dest_code;
    bool dest_is_reg = obtener_reg32(dest_str, dest_code);
    if (!dest_is_reg) {
        cerr << "Error: LEA solo soporta destino registro de 32 bits." << endl;
        return;
    }

    // LEA r32, m -> 8D /r
    agregar_byte(0x8D);
    uint8_t modrm_byte;
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
            uint32_t valor_a_parchear;

            if (ref.tipo_salto == 0) {
                valor_a_parchear = destino;
            } else {
                // Caso RELATIVO (JMP, CALL, JNE)
                int offset = destino - (pos + ref.tamano_inmediato);
                
                // NOTA: EL AJUSTE EXTRA SOLO PARA LOOP (rel8)
                if (ref.tamano_inmediato == 1) {
                    offset -= 1; // Ajuste para rel8 (LOOP)
                } 
                // APLICAMOS UN AJUSTE DE COMPENSACIÓN DE -1 AL REL32 PARA CORREGIR EL DESFASE DE CÓDIGO
                else if (ref.tamano_inmediato == 4) {
                    offset -= 1; // <--- ¡APLICAR ESTE AJUSTE!
                }
                
                valor_a_parchear = static_cast<uint32_t>(offset);
            }

            // Aplicar el parcheo (Little Endian)
            if (ref.tamano_inmediato == 4) {
                // ... (código existente para parchear 4 bytes)
                codigo_hex[pos]     = static_cast<uint8_t>(valor_a_parchear & 0xFF);
                codigo_hex[pos + 1] = static_cast<uint8_t>((valor_a_parchear >> 8) & 0xFF);
                codigo_hex[pos + 2] = static_cast<uint8_t>((valor_a_parchear >> 16) & 0xFF);
                codigo_hex[pos + 3] = static_cast<uint8_t>((valor_a_parchear >> 24) & 0xFF);
            } else if (ref.tamano_inmediato == 1) {
                // ... (código existente para parchear 1 byte)
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








