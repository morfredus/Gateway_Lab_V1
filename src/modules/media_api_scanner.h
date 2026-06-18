/**
 * MediaApiScanner — Sondage des API HTTP propriétaires des appareils
 * multimédia/IoT grand public les plus courants.
 *
 * Contrairement à PortScanner::_probeIoTApis() (qui ne sonde que le port
 * HTTP déjà détecté ouvert), ces appareils exposent leur API sur des ports
 * fixes non balayés par le scan de ports standard. On les interroge donc
 * directement, uniquement lors de la passe précise (rescanDevice), une IP
 * à la fois.
 *
 * Appareils couverts :
 *   Google Cast / Chromecast : GET :8008/setup/eureka_info        (JSON)
 *   Sonos                    : GET :1400/xml/device_description.xml (XML)
 *   Roku                     : GET :8060/query/device-info        (XML)
 *   Samsung Smart TV (Tizen) : GET :8001/api/v2/                  (JSON)
 */

#pragma once
#include <Arduino.h>

struct MediaApiResult {
    String apiType;       // "Cast", "Sonos", "Roku", "SamsungTV" ou "" si rien trouvé
    String manufacturer;   // Fabricant déduit
    String model;          // Modèle rapporté par l'API
    String friendlyName;   // Nom convivial (pièce, nom donné par l'utilisateur)
    String category;       // Suggestion de catégorie
};

class MediaApiScanner {
public:
    // Sonde les 4 API connues sur une IP unique. S'arrête à la première
    // qui répond. timeout_ms : délai de connexion/lecture par tentative.
    MediaApiResult probe(const String& ip, uint32_t timeout_ms = 600);
};

extern MediaApiScanner mediaApiScanner;
