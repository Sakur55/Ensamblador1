#include "EnsambladorIA32.hpp"
#include <cstdint>
#include <cctype>

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

bool EnsambladorIA32::es_etiqueta(const string& s) {
    return !s.empty() && s.back() == ':';
}

uint8_t EnsambladorIA32::generar_modrm(uint8_t mod, uint8_t reg, uint8_t rm) {
    return (mod << 6) | (reg << 3) | rm;
}

void EnsambladorIA32::agregar_byte(uint8_t byte) {
    codigo_hex.push_back(byte);
    contador_posicion += 1;
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

void EnsambladorIA32::procesar_etiqueta(const string& etiqueta) {
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
    }
    else if (mnem == "ADD") {
        procesar_add(resto);
    }
    else if (mnem == "SUB") {
        procesar_sub(resto);
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
        stringstream imm_ss(resto);
        string imm_str;
        imm_ss >> imm_str;
        try {
            uint32_t immediate = stoul(
                imm_str.back() == 'H'
                ? imm_str.substr(0, imm_str.size() - 1)
                : imm_str,
                nullptr,
                imm_str.back() == 'H' ? 16 : 10);
            agregar_byte(0xCD);
            agregar_byte(static_cast<uint8_t>(immediate));
        }
        catch (...) {
            cerr << "Error: Formato de INT invalido: " << linea << endl;
        }
    }
    else {
        cerr << "Advertencia: Mnemónico no soportado: " << mnem << endl;
    }
}

// -----------------------------------------------------------------------------
// MOV / ADD / SUB
// -----------------------------------------------------------------------------

void EnsambladorIA32::procesar_mov(const string& operandos) {
    stringstream ss(operandos);
    string dest_str, src_str;

    getline(ss, dest_str, ',');
    ss >> ws;
    getline(ss, src_str);

    limpiar_linea(dest_str);
    limpiar_linea(src_str);

    uint8_t dest_code, src_code;
    bool dest_is_reg = obtener_reg32(dest_str, dest_code);
    bool src_is_reg = obtener_reg32(src_str, src_code);

    // MOV REG, REG
    if (dest_is_reg && src_is_reg) {
        agregar_byte(0x89);
        uint8_t modrm = generar_modrm(0b11, src_code, dest_code);
        agregar_byte(modrm);
        return;
    }

    // MOV REG, INMEDIATO
    if (dest_is_reg) {
        try {
            uint32_t immediate;
            if (!src_str.empty() && src_str.back() == 'H') {
                immediate = stoul(src_str.substr(0, src_str.size() - 1), nullptr, 16);
            }
            else {
                immediate = stoul(src_str, nullptr, 10);
            }

            agregar_byte(0xB8 + dest_code);
            agregar_dword(immediate);
            return;
        }
        catch (...) {
            cerr << "Error: Inmediato invalido en MOV: " << src_str << endl;
            return;
        }
    }

    // MOV [ETIQUETA], REG
    if (src_is_reg && dest_str.size() > 2 &&
        dest_str.front() == '[' && dest_str.back() == ']') {

        agregar_byte(0x89); // r/m32, r32
        uint8_t modrm_byte;
        if (procesar_mem_simple(dest_str, modrm_byte, src_code, true)) return;
    }

    // MOV REG, [ETIQUETA]
    if (dest_is_reg && src_str.size() > 2 &&
        src_str.front() == '[' && src_str.back() == ']') {

        agregar_byte(0x8B); // r32, r/m32
        uint8_t modrm_byte;
        if (procesar_mem_simple(src_str, modrm_byte, dest_code, false)) return;
    }

    cerr << "Error de sintaxis o modo no soportado para MOV: " << operandos << endl;
}

void EnsambladorIA32::procesar_add(const string& operandos) {
    // Aquí podrías implementar ADD igual que SUB/MOV.
    cerr << "Advertencia: Instruccion ADD aun no implementada: " << operandos << endl;
}

void EnsambladorIA32::procesar_sub(const string& operandos) {
    stringstream ss(operandos);
    string dest_str, src_str;

    getline(ss, dest_str, ',');
    ss >> ws;
    getline(ss, src_str);

    limpiar_linea(dest_str);
    limpiar_linea(src_str);

    uint8_t dest_code, src_code;
    bool dest_is_reg = obtener_reg32(dest_str, dest_code);
    bool src_is_reg = obtener_reg32(src_str, src_code);

    // SUB REG, REG
    if (dest_is_reg && src_is_reg) {
        agregar_byte(0x29);
        uint8_t modrm = generar_modrm(0b11, src_code, dest_code);
        agregar_byte(modrm);
        return;
    }

    // SUB EAX, INMEDIATO (hex con H)
    if (dest_is_reg && dest_code == 0 && src_str.size() > 1 && src_str.back() == 'H') {
        try {
            uint32_t immediate = stoul(src_str.substr(0, src_str.size() - 1), nullptr, 16);
            agregar_byte(0x2D);
            agregar_dword(immediate);
            return;
        }
        catch (...) {
            cerr << "Error: Inmediato invalido en SUB: " << src_str << endl;
            return;
        }
    }

    // SUB [ETIQUETA], INMEDIATO pequeño
    if (!dest_is_reg && dest_str.size() > 2 &&
        dest_str.front() == '[' && dest_str.back() == ']' &&
        src_str.size() > 1 && src_str.back() == 'H') {
        try {
            uint32_t immediate = stoul(src_str.substr(0, src_str.size() - 1), nullptr, 16);
            if (immediate <= 0xFF) {
                agregar_byte(0x83);   // 83 /5  -> SUB r/m32, imm8
                uint8_t op_extension = 0b101;
                uint8_t modrm_byte;
                if (procesar_mem_simple(dest_str, modrm_byte, 0, true, op_extension)) {
                    agregar_byte(static_cast<uint8_t>(immediate));
                    return;
                }
            }
        }
        catch (...) {
            cerr << "Error: Inmediato invalido en SUB memoria: " << src_str << endl;
            return;
        }
    }

    cerr << "Error de sintaxis o modo no soportado para SUB: " << operandos << endl;
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
