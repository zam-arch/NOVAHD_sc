package com.novahd.recomp.model;

public enum ResourceState {
    UNKNOWN("Desconocido"),
    CHECKING("Verificando…"),
    READY("Listo para jugar"),
    MISSING("Faltan recursos"),
    CORRUPTED("Recursos dañados"),
    DOWNLOADING("Descargando…"),
    PAUSED("Pausado"),
    VERIFYING("Verificando integridad…"),
    EXTRACTING("Extrayendo…"),
    REPAIRING("Reparando…"),
    IMPORTING("Importando…"),
    ERROR("Error");

    private final String displayName;

    ResourceState(String displayName) {
        this.displayName = displayName;
    }

    public String getDisplayName() {
        return displayName;
    }

    public boolean isTerminal() {
        return this == READY || this == ERROR;
    }

    public boolean isActive() {
        return this == DOWNLOADING || this == VERIFYING || this == EXTRACTING || 
               this == REPAIRING || this == IMPORTING;
    }
}
