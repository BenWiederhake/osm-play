const osmium::object_id_type EXPORT_RELATIONS[] = {
    62781, // D Gesamt
    62611, // D BaWü
    2145268, // D Bay
    62422, // D Berlin
    62504, // D Brande
    62718, // D Bremen, Achtung Bremerhaven?
    451087, // D Hamburg
    62650, // D Hessen
    62774, // D Meckpom
    454192, // D Niedersachs
    62761, // D NRW
    62341, // D Rheinpfalz
    62372, // D Saarland
    62467, // D Sachsen
    62607, // D SachsAnhalt
    62775, // D Schles Hol
    62366, // D Thür
    16239, // AT Gesamt
    76909, // AT Burgenland
    52345, // AT Kärnten
    77189, // AT Niederöster
    102303, // AT Oberöster
    86539, // AT Salzburg
    35183, // AT Steiermark
    52343, // AT Tirol
    74942, // AT Vorarlberg
    109166, // AT Wien
};

static bool is_relevant_relation(osmium::object_id_type relation_id) {
    // TODO: Linear scan with 27 items … dunno if that's efficient or not.
    for (auto interesting_relation : EXPORT_RELATIONS) {
        if (interesting_relation == relation_id) {
            return true;
        }
    }
    return false;
}
