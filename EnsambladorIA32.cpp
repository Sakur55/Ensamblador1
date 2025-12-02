#include "EnsambladorIA32.hpp"
#include <cstdint>

// -----------------------------------------------------------------------------
// 📌 Inicialización
// -----------------------------------------------------------------------------

EnsambladorIA32::EnsambladorIA32() : contador_posicion(0) {
    inicializar_mapas();
}

void EnsambladorIA32::inicializar_mapas() {
    // Codificación de registros de 32 bits (REG/R/M)
    reg32_map = {
        {"EAX", 0b000}, {"ECX", 0b001}, {"EDX", 0b010}, {"EBX", 0b011},
        {"ESP", 0b100}, {"EBP", 0b101}, {"ESI", 0b110}, {"EDI", 0b111}
    };

    // Codificación de registros de 8 bits
    reg8_map = {
        {"AL", 0b000}, {"CL", 0b001}, {"DL", 0b010}, {"BL", 0b011},
        {"AH", 0b100}, {"CH", 0b101}, {"DH", 0b110}, {"BH", 0b111}
    };
}

// -----------------------------------------------------------------------------
// 🧹 Utilidades
// -----------------------------------------------------------------------------

void EnsambladorIA32::limpiar_linea(string& linea) {
    // Quitar comentarios
    size_t pos = linea.find(';');
    if (pos != string::npos) linea = linea.substr(0, pos);

    // Eliminar espacios en blanco al inicio y al final
    auto no_espacio = [](int ch) { return !isspace(ch); };
    linea.erase(linea.begin(), find_if(linea.begin(), linea.end(), no_espacio));
    linea.erase(find_if(linea.rbegin(), linea.rend(), no_espacio).base(), linea.end());

    // Convertir a mayúsculas para facilitar el análisis (opcional)
    transform(linea.begin(), linea.end(), linea.begin(), ::toupper);
}

bool EnsambladorIA32::es_etiqueta(const string& s) {
    // Una etiqueta termina con ':'
    return !s.empty() && s.back() == ':';
}

uint8_t EnsambladorIA32::generar_modrm(uint8_t mod, uint8_t reg, uint8_t rm) {
    // Estructura: Mod(7-6) | Reg/Opcode(5-3) | R/M(2-0)
    return (mod << 6) | (reg << 3) | rm;
}

void EnsambladorIA32::agregar_byte(uint8_t byte) {
    codigo_hex.push_back(byte);
    contador_posicion += 1;
}

void EnsambladorIA32::agregar_dword(uint32_t dword) {
    // Little-endian para valores de 32 bits
    agregar_byte(static_cast<uint8_t>(dword & 0xFF));
    agregar_byte(static_cast<uint8_t>((dword >> 8) & 0xFF));
    agregar_byte(static_cast<uint8_t>((dword >> 16) & 0xFF));
    agregar_byte(static_cast<uint8_t>((dword >> 24) & 0xFF));
}

bool EnsambladorIA32::obtener_reg32(const string& op, uint8_t& reg_code) {
    if (reg32_map.count(op)) {
        reg_code = reg32_map.at(op);
        return true;
    }
    return false;
}

// Procesa una referencia a memoria simple del tipo [ETIQUETA] o [ETIQUETA+DISP]
bool EnsambladorIA32::procesar_mem_simple(const string& operando,
                                          uint8_t& modrm_byte,
                                          const uint8_t reg_code,
                                          bool es_destino,
                                          uint8_t op_extension) {
    /*
        Implementación simplificada:
        - Asumimos operando del tipo [ETIQUETA]
        - Se genera un ModR/M con:
            Mod = 00 (direccionamiento con desplazamiento de 32 bits)
            R/M = 101 (dirección directa)
        - El registro (reg/opcode) se elige según reg_code u op_extension
    */

    string op = operando;
    if (op.front() == '[' && op.back() == ']') {
        op = op.substr(1, op.size() - 2); // Quitar corchetes
    } else {
        return false;
    }

    // El operando debe ser una etiqueta (sin registros dentro)
    string etiqueta = op;

    // Reg/opcode field
    uint8_t mod = 0b00;
    uint8_t rm  = 0b101; // Dirección absoluta

    uint8_t reg_field = es_destino ? op_extension : reg_code;

    modrm_byte = generar_modrm(mod, reg_field, rm);

    // Registrar referencia pendiente para la etiqueta
    ReferenciaPendiente ref;
    ref.posicion = contador_posicion + 1; // después del opcode
    ref.tamano_inmediato = 4;
    ref.tipo_salto = 0; // Absoluto
    referencias_pendientes[etiqueta].push_back(ref);

    // Reservar espacio para el desplazamiento (4 bytes)
    agregar_dword(0);

    return true;
}

// -----------------------------------------------------------------------------
// 🧠 Procesamiento de líneas
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

void EnsambladorIA32::procesar_etiqueta(const string& etiqueta) {
    // La etiqueta se almacena con la posición actual del Contador de Posición (CP)
    tabla_simbolos[etiqueta] = contador_posicion;
}

void EnsambladorIA32::procesar_instruccion(const string& linea) {
    stringstream ss(linea);
    string mnem;
    ss >> mnem;

    string resto;
    getline(ss, resto);
    limpiar_linea(resto);

    if (mnem == "MOV") {
        procesar_mov(resto);
    } else if (mnem == "ADD") {
        procesar_add(resto);
    } else if (mnem == "SUB") {
        procesar_sub(resto);
    } else if (mnem == "JMP") {
        procesar_jmp(resto);
    } else if (mnem == "JE" || mnem == "JZ" ||
               mnem == "JNE" || mnem == "JNZ" ||
               mnem == "JLE" || mnem == "JL" ||
               mnem == "JA"  || mnem == "JAE" ||
               mnem == "JB"  || mnem == "JBE" ||
               mnem == "JG"  || mnem == "JGE") {
        procesar_condicional(mnem, resto);
    } else if (mnem == "INT") {
        // Implementación simple INT imm8 (CD imm8)
        stringstream imm_ss(resto);
        string imm_str;
        imm_ss >> imm_str;
        try {
            uint32_t immediate = stoul(imm_str, nullptr, 16);
            agregar_byte(0xCD);
            agregar_byte(static_cast<uint8_t>(immediate));
        } catch (...) {
            cerr << "Error: Formato de INT inválido: " << linea << endl;
        }
    } else {
        cerr << "Advertencia: Mnemónico no soportado: " << mnem << endl;
    }
}

// -----------------------------------------------------------------------------
// 🧾 Procesamiento específico de instrucciones
// -----------------------------------------------------------------------------

void EnsambladorIA32::procesar_mov(const string& operandos) {
    // Formatos soportados (simplificados):
    // 1. MOV REG, REG
    // 2. MOV REG, INMEDIATO (hexadecimal)
    // 3. MOV [ETIQUETA], REG
    // 4. MOV REG, [ETIQUETA]

    stringstream ss(operandos);
    string dest_str, src_str;

    getline(ss, dest_str, ',');
    ss >> ws;
    getline(ss, src_str);

    limpiar_linea(dest_str);
    limpiar_linea(src_str);

    uint8_t dest_code, src_code;
    bool dest_is_reg = obtener_reg32(dest_str, dest_code);
    bool src_is_reg  = obtener_reg32(src_str,  src_code);

    // 1. MOV REG, REG
    if (dest_is_reg && src_is_reg) {
        agregar_byte(0x89); // Opcode 89 (r/m32, r32)
        uint8_t modrm = generar_modrm(0b11, src_code, dest_code); // Mod=11 (Reg-Reg)
        agregar_byte(modrm);
        return;
    }

    // 2. MOV REG, INMEDIATO
    if (dest_is_reg && src_str.size() > 1 && src_str.back() == 'H') {
        try {
            uint32_t immediate = stoul(src_str.substr(0, src_str.size() - 1), nullptr, 16);
            agregar_byte(0xB8 + dest_code); // B8+rd
            agregar_dword(immediate);
            return;
        } catch (...) {
            cerr << "Error: Inmediato inválido en MOV: " << src_str << endl;
            return;
        }
    }

    // 3. MOV Memoria-Registro (MOV [ETIQUETA], EAX)
    if (src_is_reg && dest_str.size() > 2 && dest_str.front() == '[' && dest_str.back() == ']') {
        agregar_byte(0x89); // Opcode 89 (r/m32, r32)
        uint8_t modrm_byte;
        if (procesar_mem_simple(dest_str, modrm_byte, src_code, true)) return;
    }
    
    // 4. MOV Registro-Memoria (MOV EAX, [ETIQUETA])
    if (dest_is_reg && src_str.size() > 2 && src_str.front() == '[' && src_str.back() == ']') {
        agregar_byte(0x8B); // Opcode 8B (r32, r/m32)
        uint8_t modrm_byte;
        if (procesar_mem_simple(src_str, modrm_byte, dest_code, false)) return;
    }
    
    cerr << "Error de sintaxis o modo no soportado para MOV: " << operandos << endl;
}

void EnsambladorIA32::procesar_add(const string& operandos) {
    // Aquí podrías implementar ADD de forma similar a MOV/SUB.
    // Por simplicidad, dejamos un mensaje para indicar que no está implementado.
    cerr << "Advertencia: Instrucción ADD aún no implementada: " << operandos << endl;
}

void EnsambladorIA32::procesar_sub(const string& operandos) {
    // Formatos soportados (simplificados):
    // 1. SUB REG, REG
    // 2. SUB REG, INMEDIATO
    // 3. SUB [ETIQUETA], INMEDIATO

    stringstream ss(operandos);
    string dest_str, src_str;

    getline(ss, dest_str, ',');
    ss >> ws;
    getline(ss, src_str);

    limpiar_linea(dest_str);
    limpiar_linea(src_str);

    uint8_t dest_code, src_code;
    bool dest_is_reg = obtener_reg32(dest_str, dest_code);
    bool src_is_reg  = obtener_reg32(src_str,  src_code);

    // 1. SUB REG, REG
    if (dest_is_reg && src_is_reg) {
        agregar_byte(0x29); // SUB r/m32, r32
        uint8_t modrm = generar_modrm(0b11, src_code, dest_code);
        agregar_byte(modrm);
        return;
    }

    // 2. SUB EAX, INMEDIATO (caso especial)
    if (dest_is_reg && dest_code == 0 && src_str.size() > 1 && src_str.back() == 'H') {
        try {
            uint32_t immediate = stoul(src_str.substr(0, src_str.size() - 1), nullptr, 16);
            agregar_byte(0x2D); // SUB EAX, imm32
            agregar_dword(immediate);
            return;
        } catch (...) {
            cerr << "Error: Inmediato inválido en SUB: " << src_str << endl;
            return;
        }
    }

    // 3. SUB [ETIQUETA], INMEDIATO (inmediato pequeño)
    if (!dest_is_reg && dest_str.size() > 2 && dest_str.front() == '[' && dest_str.back() == ']'
        && src_str.size() > 1 && src_str.back() == 'H') {
        try {
            uint32_t immediate = stoul(src_str.substr(0, src_str.size() - 1), nullptr, 16);
            if (immediate <= 0xFF) {
                // Opcode: 83 /5 (SUB r/m32, imm8)
                agregar_byte(0x83);
                uint8_t op_extension = 0b101; // /5 para SUB
                uint8_t modrm_byte;
                if (procesar_mem_simple(dest_str, modrm_byte, 0, true, op_extension)) {
                    agregar_byte(static_cast<uint8_t>(immediate)); // imm8
                    return;
                }
            }
        } catch (...) {
            cerr << "Error: Inmediato inválido en SUB memoria: " << src_str << endl;
            return;
        }
    }

    cerr << "Error de sintaxis o modo no soportado para SUB: " << operandos << endl;
}

void EnsambladorIA32::procesar_jmp(string operandos) {
    // JMP near (32-bit relativo)
    limpiar_linea(operandos);
    string etiqueta = operandos;

    agregar_byte(0xE9); // Opcode E9 
    
    int tamano_instruccion = 5; // E9 (1 byte) + desplazamiento (4 bytes)
    int posicion_referencia = contador_posicion;
    contador_posicion += 4; // Avanzar el contador para el desplazamiento

    if (tabla_simbolos.count(etiqueta)) {
        int destino = tabla_simbolos[etiqueta];
        int offset = destino - (contador_posicion);
        agregar_dword(static_cast<uint32_t>(offset));
    } else {
        ReferenciaPendiente ref;
        ref.posicion = posicion_referencia;
        ref.tamano_inmediato = 4;
        ref.tipo_salto = 1; // Relativo
        referencias_pendientes[etiqueta].push_back(ref);
        agregar_dword(0); // Se parcheará después
    }
}

void EnsambladorIA32::procesar_condicional(const string& mnem, string operandos) {
    // Se asume el salto largo (near, 32-bit relativo)
    limpiar_linea(operandos);
    string etiqueta = operandos;
    
    uint8_t opcode_byte1 = 0x0F;
    uint8_t opcode_byte2;

    // Mapeo de mnemónicos condicionales a su segundo byte de opcode
    if (mnem == "JE" || mnem == "JZ") opcode_byte2 = 0x84;
    else if (mnem == "JNE" || mnem == "JNZ") opcode_byte2 = 0x85;
    else if (mnem == "JLE") opcode_byte2 = 0x8E;
    else if (mnem == "JL") opcode_byte2 = 0x8C;
    else if (mnem == "JA") opcode_byte2 = 0x87;
    else if (mnem == "JAE") opcode_byte2 = 0x83;
    else if (mnem == "JB") opcode_byte2 = 0x82;
    else if (mnem == "JBE") opcode_byte2 = 0x86;
    else if (mnem == "JG") opcode_byte2 = 0x8F;
    else if (mnem == "JGE") opcode_byte2 = 0x8D;
    else {
        cerr << "Error: Mnemónico condicional no soportado: " << mnem << endl;
        return;
    }

    agregar_byte(opcode_byte1);
    agregar_byte(opcode_byte2);

    int tamano_instruccion = 6; // 0F 8X + desplazamiento (4 bytes)
    int posicion_referencia = contador_posicion;
    contador_posicion += 4;

    if (tabla_simbolos.count(etiqueta)) {
        int destino = tabla_simbolos[etiqueta];
        int offset = destino - contador_posicion;
        agregar_dword(static_cast<uint32_t>(offset));
    } else {
        ReferenciaPendiente ref;
        ref.posicion = posicion_referencia;
        ref.tamano_inmediato = 4;
        ref.tipo_salto = 1; // Relativo
        referencias_pendientes[etiqueta].push_back(ref);
        agregar_dword(0);
    }
}

// -----------------------------------------------------------------------------
// 🧩 Resolución de referencias pendientes
// -----------------------------------------------------------------------------

void EnsambladorIA32::resolver_referencias_pendientes() {
    for (auto& par : referencias_pendientes) {
        const string& etiqueta = par.first;
        auto& lista_refs = par.second;

        if (!tabla_simbolos.count(etiqueta)) {
            cerr << "Advertencia: Etiqueta no definida '" << etiqueta << "'. Referencia no resuelta." << endl;
            continue;
        }

        int destino = tabla_simbolos[etiqueta];

        for (auto& ref : lista_refs) {
            int pos = ref.posicion;
            uint32_t valor_a_parchear;

            if (ref.tipo_salto == 0) {
                // Salto absoluto
                valor_a_parchear = destino;
            } else {
                // Salto relativo
                int offset = destino - (pos + ref.tamano_inmediato);
                valor_a_parchear = static_cast<uint32_t>(offset);
            }

            // Escribir el valor en codigo_hex (little-endian)
            if (pos + 3 < static_cast<int>(codigo_hex.size())) {
                codigo_hex[pos]     = static_cast<uint8_t>(valor_a_parchear & 0xFF);
                codigo_hex[pos + 1] = static_cast<uint8_t>((valor_a_parchear >> 8) & 0xFF);
                codigo_hex[pos + 2] = static_cast<uint8_t>((valor_a_parchear >> 16) & 0xFF);
                codigo_hex[pos + 3] = static_cast<uint8_t>((valor_a_parchear >> 24) & 0xFF);
            } else {
                cerr << "Error: Referencia fuera de rango al parchear." << endl;
            }
        }
    }
}

// -----------------------------------------------------------------------------
// 🏗️ Proceso de ensamblado principal
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
    for (uint8_t byte : codigo_hex) {
        f << setw(2) << static_cast<int>(byte) << " ";
    }
    f << endl;
    f.close();
}

void EnsambladorIA32::generar_reportes() {
    // Generar Tabla de Símbolos
    ofstream sym("simbolos.txt");
    sym << "Tabla de Símbolos:" << endl;
    for (const auto& par : tabla_simbolos) {
        sym << par.first << " -> " << par.second << endl;
    }
    sym.close();

    // Generar Tabla de Referencias Pendientes
    ofstream refs("referencias.txt");
    refs << "Tabla de Referencias Pendientes:" << endl;
    for (const auto& par : referencias_pendientes) {
        const string& etiqueta = par.first;
        const auto& lista = par.second;
        for (const auto& ref : lista) {
            refs << "Etiqueta: " << etiqueta
                 << ", Posicion: " << ref.posicion
                 << ", Tamano: " << ref.tamano_inmediato
                 << ", Tipo: " << (ref.tipo_salto == 0 ? "ABSOLUTO" : "RELATIVO")
                 << endl;
        }
    }
    refs.close();
}

// -----------------------------------------------------------------------------
// 🧪 main de prueba
// -----------------------------------------------------------------------------

int main() {
    // 1. Crear un archivo ASM de ejemplo
    ofstream asm_file("programa.asm");
    asm_file << "SECTION .TEXT" << endl;
    asm_file << "GLOBAL _START" << endl;
    asm_file << "_START:" << endl;
    asm_file << "MOV EAX, 1" << endl;           // B8 01 00 00 00
    asm_file << "MOV EBX, 0" << endl;           // BB 00 00 00 00
    asm_file << "CALL ETIQUETA_LL" << endl;     // (no implementado en este ensamblador, solo ejemplo)
    asm_file << "SUB EAX, 1H" << endl;          // 2D 01 00 00 00
    asm_file << "JE ETIQUETA_FIN" << endl;      // 0F 84 xx xx xx xx
    asm_file << "ETIQUETA_LL:" << endl;
    asm_file << "MOV EAX, 5H" << endl;
    asm_file << "SUB EAX, 1H" << endl;
    asm_file << "JNE ETIQUETA_LL" << endl;
    asm_file << "ETIQUETA_FIN:" << endl;
    asm_file << "INT 80H" << endl;
    asm_file << "SECTION .DATA" << endl;
    asm_file << "VAR_DATA: DD 0" << endl;
    asm_file.close();

    // 2. Ejecutar el ensamblador
    EnsambladorIA32 ensamblador;
    cout << "Iniciando ensamblado en una sola pasada (EnsambladorIA32.cpp)..." << endl;
    ensamblador.ensamblar("programa.asm");
    
    cout << "Resolviendo referencias pendientes..." << endl;
    ensamblador.resolver_referencias_pendientes();
    
    cout << "Generando código hexadecimal y reportes..." << endl;
    ensamblador.generar_hex("programa.hex");
    ensamblador.generar_reportes();
    
    cout << "Proceso completado. Revise programa.hex, simbolos.txt y referencias.txt" << endl;

    return 0;
}
