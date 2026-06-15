| Risque | Mitigation | 
| :-------- | :-------- |
| std::vector dans ISR/task     | Protéger TOUJOURS par xSemaphoreTake avant lecture/écriture   |
| String non thread-safe     | Le scanner ne touche que _results privé ; getResults() retourne une copie value   |
| OTA + scan concurrent     | otaMgr.isUpdating() → scanner suspend sa tâche avec vTaskSuspend   | 
| Stack overflow tâche scanner     | Valider avec uxTaskGetStackHighWaterMark() en debug   | 
| etharp_find_addr() dépréciée lwIP 2.2+     | Utiliser esp_netif_get_all_ip6() + netif_find() selon version SDK   | 
| Redessin IP après reconnexion     | wifi_manager doit re-notifier network_scanner.begin() avec la nouvelle IP   |

# Décisions d'architecture

## HTML en PROGMEM
Choisi pour éviter SPIFFS/LittleFS et simplifier les déploiements OTA.

## API REST locale
L'interface web consomme exclusivement l'API REST afin de pouvoir être remplacée ultérieurement par une application externe.

## Scanner réseau séparé
Exécuté dans sa propre tâche FreeRTOS afin de ne jamais bloquer :
- le serveur web
- OTA
- Wi-Fi