#!/bin/bash


sed "s/__title__/I miei pasticci di Natale/g" ./utility/kids_addStreamTemplate.json | sed "s/__url__/https:\/\/b70cb04c54ab478189e9d8ee45637b13.msvdn.net\/dom7\/podcastcdn\/teche_root\/PLAYRAI_TECHE_CINEMA_HD\/13697080_,1200,1800,2400,.mp4.csmil\/playlist.m3u8/g" | sed "s/__description__/Claire è una scrittrice di libri di cucina, un'esperta di decorazioni e un personaggio televisivo il cui nome è sinonimo di perfezione. Mentre Claire contempla l'idea di ritirarsi, sua figlia Candace è pronta a diventare il nuovo volto del marchio Livingston ma c'è un problema di non poco conto: non sa cucinare, cucire o fare tutto ciò per cui la madre è divenuta famosa./g" | sed "s/__year__/2018/g" | sed "s/__categories__/\"Natale-Romantico\"/g" | sed "s/__language__/Italiano/g" | sed "s/__duration__/1h25m/g" > ./outputAddStream.json
curl -k -u 1:HNVOoVhHx0yoWNIxFu-ThBA1vAPEKWsxneGgze6eoddEv9aB1xp9NzsBKDBDMEFN -d @./outputAddStream.json -H "Content-Type: application/json" https://mms-api.cibortv-mms.com/catramms/1.0.1/conf/stream

sed "s/__title__/Il miglior Natale della mia vita/g" ./utility/kids_addStreamTemplate.json | sed "s/__url__/https:\/\/b70cb04c54ab478189e9d8ee45637b13.msvdn.net\/dom7\/podcastcdn\/teche_root\/PLAYRAI_TECHE_CINEMA_HD\/13689340_,1200,1800,2400,.mp4.csmil\/playlist.m3u8/g" | sed "s/__description__/Dopo quattro anni a New York, Becca torna nella natìa Baynesville per trascorrere il Natale con nonna Jean, rivedere tante altre persone care e provare ad evitare Max, il suo grande amore che la lasciò partire./g" | sed "s/__year__/2019/g" | sed "s/__categories__/\"Natale-Romantico\"/g" | sed "s/__language__/Italiano/g" | sed "s/__duration__/1h23m/g" > ./outputAddStream.json
curl -k -u 1:HNVOoVhHx0yoWNIxFu-ThBA1vAPEKWsxneGgze6eoddEv9aB1xp9NzsBKDBDMEFN -d @./outputAddStream.json -H "Content-Type: application/json" https://mms-api.cibortv-mms.com/catramms/1.0.1/conf/stream

sed "s/__title__/Il mio desiderio per Natale/g" ./utility/kids_addStreamTemplate.json | sed "s/__url__/https:\/\/b70cb04c54ab478189e9d8ee45637b13.msvdn.net\/dom6\/podcastcdn\/teche_root\/PLAYRAI_TECHE_CINEMA_HD\/j2BJAcL1mZgoCpzKmt5c8AeeqqEEqualeeqqEEqual_,1200,1800,2400,.mp4.csmil\/playlist.m3u8/g" | sed "s/__description__/Jessica Morgan, una giovane donna in carriera, decide di passare la vigilia di Natale nella cittadina di Glenbrooke, convinta che questo potrà farle sentire più vivo il ricordo dei suoi genitori. Qui incontrerà un affascinante pompiere./g" | sed "s/__year__/2020/g" | sed "s/__categories__/\"Natale-Romantico\"/g" | sed "s/__language__/Italiano/g" | sed "s/__duration__/1h25m/g" > ./outputAddStream.json
curl -k -u 1:HNVOoVhHx0yoWNIxFu-ThBA1vAPEKWsxneGgze6eoddEv9aB1xp9NzsBKDBDMEFN -d @./outputAddStream.json -H "Content-Type: application/json" https://mms-api.cibortv-mms.com/catramms/1.0.1/conf/stream

sed "s/__title__/Il mio principe di Natale/g" ./utility/kids_addStreamTemplate.json | sed "s/__url__/https:\/\/b70cb04c54ab478189e9d8ee45637b13.msvdn.net\/dom6\/podcastcdn\/teche_root\/PLAYRAI_TECHE_CINEMA_HD\/13620970_,1200,1800,2400,.mp4.csmil\/playlist.m3u8/g" | sed "s/__description__/Senza una compagna per l'imminente ballo di Natale, re Charles di Baltania, un giovane trentanovenne vedovo, decide di mettersi sulle tracce di Allison, la fidanzata americana dei tempi del college./g" | sed "s/__year__/2017/g" | sed "s/__categories__/\"Natale-Romantico\"/g" | sed "s/__language__/Italiano/g" | sed "s/__duration__/1h25m/g" > ./outputAddStream.json
curl -k -u 1:HNVOoVhHx0yoWNIxFu-ThBA1vAPEKWsxneGgze6eoddEv9aB1xp9NzsBKDBDMEFN -d @./outputAddStream.json -H "Content-Type: application/json" https://mms-api.cibortv-mms.com/catramms/1.0.1/conf/stream

sed "s/__title__/Il Natale che ho dimenticato/g" ./utility/kids_addStreamTemplate.json | sed "s/__url__/https:\/\/b70cb04c54ab478189e9d8ee45637b13.msvdn.net\/dom7\/podcastcdn\/teche_root\/PLAYRAI_TECHE_CINEMA_HD\/Pyfu0xtpPpPluss94czBuUZuEOpggeeqqEEqualeeqqEEqual_,1200,1800,2400,.mp4.csmil\/playlist.m3u8/g" | sed "s/__description__/Otto giorni prima di Natale, Lucy ha un incidente e al risveglio, con un bernoccolo in testa e un inspiegabile abito da sposa, scopre di aver perso la memoria e di non ricordare nulla dei suoi ultimi due anni di vita. Zack, il suo fidanzato di allora, la aiuterà a ricordare./g" | sed "s/__year__/2019/g" | sed "s/__categories__/\"Natale-Romantico\"/g" | sed "s/__language__/Italiano/g" | sed "s/__duration__/1h25m/g" > ./outputAddStream.json
curl -k -u 1:HNVOoVhHx0yoWNIxFu-ThBA1vAPEKWsxneGgze6eoddEv9aB1xp9NzsBKDBDMEFN -d @./outputAddStream.json -H "Content-Type: application/json" https://mms-api.cibortv-mms.com/catramms/1.0.1/conf/stream

sed "s/__title__/Il Natale che ho sempre desiderato/g" ./utility/kids_addStreamTemplate.json | sed "s/__url__/https:\/\/b70cb04c54ab478189e9d8ee45637b13.msvdn.net\/dom6\/podcastcdn\/teche_root\/PLAYRAI_TECHE_CINEMA_HD\/13621537_,1200,1800,2400,.mp4.csmil\/playlist.m3u8/g" | sed "s/__description__/Dopo la fine di un amore ed il recente licenziamento, Lizzie, romantica sognatrice, amante del Natale, ottiene un lavoro come amministratrice della prestigiosa tenuta Ashford di proprietà della famiglia Marley./g" | sed "s/__year__/2017/g" | sed "s/__categories__/\"Natale-Romantico\"/g" | sed "s/__language__/Italiano/g" | sed "s/__duration__/1h25m/g" > ./outputAddStream.json
curl -k -u 1:HNVOoVhHx0yoWNIxFu-ThBA1vAPEKWsxneGgze6eoddEv9aB1xp9NzsBKDBDMEFN -d @./outputAddStream.json -H "Content-Type: application/json" https://mms-api.cibortv-mms.com/catramms/1.0.1/conf/stream

sed "s/__title__/Il Natale della mamma imperfetta/g" ./utility/kids_addStreamTemplate.json | sed "s/__url__/https:\/\/b70cb04c54ab478189e9d8ee45637b13.msvdn.net\/dom6\/podcastcdn\/teche_root\/PLAYRAI_TECHE_CINEMA_SD_2\/6165163_,1200,1800,2400,.mp4.csmil\/playlist.m3u8/g" | sed "s/__description__/Si avvicina il Natale e Chiara Guerrieri, come tutte le mamme imperfette, è alle prese con i preparativi, i regali introvabili, le lotte con i genitori perfetti e l'organizzazione del cenone./g" | sed "s/__year__/2013/g" | sed "s/__categories__/\"Natale-Commedia\"/g" | sed "s/__language__/Italiano/g" | sed "s/__duration__/1h36m/g" > ./outputAddStream.json
curl -k -u 1:HNVOoVhHx0yoWNIxFu-ThBA1vAPEKWsxneGgze6eoddEv9aB1xp9NzsBKDBDMEFN -d @./outputAddStream.json -H "Content-Type: application/json" https://mms-api.cibortv-mms.com/catramms/1.0.1/conf/stream

sed "s/__title__/Natale tra le stelle/g" ./utility/kids_addStreamTemplate.json | sed "s/__url__/https:\/\/b70cb04c54ab478189e9d8ee45637b13.msvdn.net\/dom6\/podcastcdn\/teche_root\/PLAYRAI_TECHE_CINEMA_HD\/VaRtpPpPlussTq4oskTXmtRAHhZcAeeqqEEqualeeqqEEqual_,1200,1800,2400,.mp4.csmil\/playlist.m3u8/g" | sed "s/__description__/Ellie ha il suo lavoro in città ma ciò non sembra soddisfarla. Viene richiamata a casa per aiutare il padre con il loro vivaio di stelle di Natale. Subito dopo il suo ritorno, scopre che per vari motivi, legati alla stanchezza e al calo delle vendite degli ultimi anni, il padre ha deciso di vendere l'attività. Le cose vanno ancora peggio quando la data di consegna di 10 mila piante per la sfilata annuale si avvicina ma le stelle di Natale tardano a cambiare colore e a diventare rosse./g" | sed "s/__year__/2018/g" | sed "s/__categories__/\"Natale-Romantico\"/g" | sed "s/__language__/Italiano/g" | sed "s/__duration__/1h24m/g" > ./outputAddStream.json
curl -k -u 1:HNVOoVhHx0yoWNIxFu-ThBA1vAPEKWsxneGgze6eoddEv9aB1xp9NzsBKDBDMEFN -d @./outputAddStream.json -H "Content-Type: application/json" https://mms-api.cibortv-mms.com/catramms/1.0.1/conf/stream

sed "s/__title__/Quel Natale che ci ha fatto incontrare/g" ./utility/kids_addStreamTemplate.json | sed "s/__url__/https:\/\/b70cb04c54ab478189e9d8ee45637b13.msvdn.net\/dom6\/podcastcdn\/teche_root\/PLAYRAI_TECHE_CINEMA_HD\/13690612_,1200,1800,2400,.mp4.csmil\/playlist.m3u8/g" | sed "s/__description__/Sophie, una fotografa professionista di San Francisco, si ritroverebbe sola per Natale dopo che il fidanzato Brent ha accettato un incarico in Asia. Per scongiurare ciò, si prende un paio di giorni per fare visita alla nonna e prendersi cura di lei. Una volta lì, si ritrova a far temporaneamente da tata a Troy, un bambino di nove anni, e a scoprirsi innamorata di David, lo zio del piccolo./g" | sed "s/__year__/2019/g" | sed "s/__categories__/\"Natale-Romantico\"/g" | sed "s/__language__/Italiano/g" | sed "s/__duration__/1h24m/g" > ./outputAddStream.json
curl -k -u 1:HNVOoVhHx0yoWNIxFu-ThBA1vAPEKWsxneGgze6eoddEv9aB1xp9NzsBKDBDMEFN -d @./outputAddStream.json -H "Content-Type: application/json" https://mms-api.cibortv-mms.com/catramms/1.0.1/conf/stream

sed "s/__title__/Se scappo mi sposo a Natale/g" ./utility/kids_addStreamTemplate.json | sed "s/__url__/https:\/\/b70cb04c54ab478189e9d8ee45637b13.msvdn.net\/dom6\/podcastcdn\/teche_root\/PLAYRAI_TECHE_CINEMA_HD\/13619608_,1200,1800,2400,.mp4.csmil\/playlist.m3u8/g" | sed "s/__description__/Poco dopo essersi sposati, Alex rivela a Kate un suo segreto che costringe la ragazza a lasciare il suo sposo e scappare via. Si rifugia in montagna per riflettere e conosce Jason, un ex campione di sci./g" | sed "s/__year__/2017/g" | sed "s/__categories__/\"Natale-Romantico\"/g" | sed "s/__language__/Italiano/g" | sed "s/__duration__/1h26m/g" > ./outputAddStream.json
curl -k -u 1:HNVOoVhHx0yoWNIxFu-ThBA1vAPEKWsxneGgze6eoddEv9aB1xp9NzsBKDBDMEFN -d @./outputAddStream.json -H "Content-Type: application/json" https://mms-api.cibortv-mms.com/catramms/1.0.1/conf/stream

sed "s/__title__/Un bebè per Natale/g" ./utility/kids_addStreamTemplate.json | sed "s/__url__/https:\/\/b70cb04c54ab478189e9d8ee45637b13.msvdn.net\/dom7\/podcastcdn\/teche_root\/PLAYRAI_TECHE_CINEMA_HD\/13635540_,1200,1800,2400,.mp4.csmil\/playlist.m3u8/g" | sed "s/__description__/E' Natale e Noemie, a causa di un incidente, si risveglia dal coma dopo sei mesi. La sorpresa, al risveglio, è che lei è incinta. Non ricordando niente fino ad una settimana prima dall'incidente, non sa chi possa essere il padre. Maureen, la sua migliore amica, decide di raccontarle tutto: in effetti Noemie era fidanzata con Olivier, un uomo tutto d'un pezzo, direttore di banca, ma al ritorno del suo vecchio amore, Yann, che l'aveva lasciata per seguire i suoi sogni, lei ha avuto dei dubbi./g" | sed "s/__year__/2018/g" | sed "s/__categories__/\"Natale-Romantico\"/g" | sed "s/__language__/Italiano/g" | sed "s/__duration__/1h33m/g" > ./outputAddStream.json
curl -k -u 1:HNVOoVhHx0yoWNIxFu-ThBA1vAPEKWsxneGgze6eoddEv9aB1xp9NzsBKDBDMEFN -d @./outputAddStream.json -H "Content-Type: application/json" https://mms-api.cibortv-mms.com/catramms/1.0.1/conf/stream

sed "s/__title__/Un gioioso Natale/g" ./utility/kids_addStreamTemplate.json | sed "s/__url__/https:\/\/b70cb04c54ab478189e9d8ee45637b13.msvdn.net\/dom6\/podcastcdn\/teche_root\/PLAYRAI_TECHE_CINEMA_HD\/Svj8Mf10yWAr7xxiAwXZnAeeqqEEqualeeqqEEqual_,1200,1800,2400,.mp4.csmil\/playlist.m3u8/g" | sed "s/__description__/Lauren e la sua migliore amica Colleen pensano di avere un lavoro meraviglioso, perché organizzano eventi ed in particolare sono specializzate nelle decorazioni natalizie. Le due socie e amiche sono molto eccitate all'idea di avere come nuovi clienti delle persone che vantano, addirittura, una lontanissima parentela con la Regina d'Inghilterra. La loro governante, una donna molto severa ed esigente, le mette a dura prova. /g" | sed "s/__year__/2019/g" | sed "s/__categories__/\"Natale-Romantico\"/g" | sed "s/__language__/Italiano/g" | sed "s/__duration__/1h22m/g" > ./outputAddStream.json
curl -k -u 1:HNVOoVhHx0yoWNIxFu-ThBA1vAPEKWsxneGgze6eoddEv9aB1xp9NzsBKDBDMEFN -d @./outputAddStream.json -H "Content-Type: application/json" https://mms-api.cibortv-mms.com/catramms/1.0.1/conf/stream

sed "s/__title__/Un Natale molto bizzarro/g" ./utility/kids_addStreamTemplate.json | sed "s/__url__/https:\/\/b70cb04c54ab478189e9d8ee45637b13.msvdn.net\/dom7\/podcastcdn\/teche_root\/PLAYRAI_TECHE_CINEMA_HD\/13621874_,1200,1800,2400,.mp4.csmil\/playlist.m3u8?/g" | sed "s/__description__/Kate è una pasticcera che non ha modo di godersi il Natale a causa del suo lavoro. Randolf Drosselmeyer, un commerciante di articoli natalizi, le regala uno Schiaccianoci di legno che, magicamente, prende vita./g" | sed "s/__year__/2018/g" | sed "s/__categories__/\"Natale-Romantico\"/g" | sed "s/__language__/Italiano/g" | sed "s/__duration__/1h22m/g" > ./outputAddStream.json
curl -k -u 1:HNVOoVhHx0yoWNIxFu-ThBA1vAPEKWsxneGgze6eoddEv9aB1xp9NzsBKDBDMEFN -d @./outputAddStream.json -H "Content-Type: application/json" https://mms-api.cibortv-mms.com/catramms/1.0.1/conf/stream

sed "s/__title__/Lo strano Natale di Blanca Snow/g" ./utility/kids_addStreamTemplate.json | sed "s/__url__/https:\/\/b70cb04c54ab478189e9d8ee45637b13.msvdn.net\/dom6\/podcastcdn\/teche_root\/PLAYRAI_TECHE_CINEMA_HD\/13622119_,1200,1800,2400,.mp4.csmil\/playlist.m3u8/g" | sed "s/__description__/E' quasi arrivato il 25esimo compleanno di Blanca, una dolce ragazza rimasta orfana che vive con la perfida matrigna Victoria. Quest'ultima fa di tutto per escludere la ragazza dall'eredità del defunto padre./g" | sed "s/__year__/2018/g" | sed "s/__categories__/\"Natale-Romantico\"/g" | sed "s/__language__/Italiano/g" | sed "s/__duration__/1h24m/g" > ./outputAddStream.json
curl -k -u 1:HNVOoVhHx0yoWNIxFu-ThBA1vAPEKWsxneGgze6eoddEv9aB1xp9NzsBKDBDMEFN -d @./outputAddStream.json -H "Content-Type: application/json" https://mms-api.cibortv-mms.com/catramms/1.0.1/conf/stream

sed "s/__title__/Un marito per Natale/g" ./utility/kids_addStreamTemplate.json | sed "s/__url__/https:\/\/b70cb04c54ab478189e9d8ee45637b13.msvdn.net\/dom7\/podcastcdn\/teche_root\/PLAYRAI_TECHE_CINEMA_HD\/13736388_,1200,1800,2400,.mp4.csmil\/playlist.m3u8/g" | sed "s/__description__/Mancano due settimane alla vigilia di Natale quando la graphic designer Brooke Harris scopre che per lei c'è in vista una promozione a condizione che il suo vecchio posto venga preso da Roger, collega della sede di Londra, e che lei lo sposi in segreto per fargli avere il visto lavorativo. Brooke e Roger accettano, ma i colleghi, gli ex fidanzati e gli agenti dell'immigrazione li tengono d'occhio./g" | sed "s/__year__/2016/g" | sed "s/__categories__/\"Natale-Romantico\"/g" | sed "s/__language__/Italiano/g" | sed "s/__duration__/1h24m/g" > ./outputAddStream.json
curl -k -u 1:HNVOoVhHx0yoWNIxFu-ThBA1vAPEKWsxneGgze6eoddEv9aB1xp9NzsBKDBDMEFN -d @./outputAddStream.json -H "Content-Type: application/json" https://mms-api.cibortv-mms.com/catramms/1.0.1/conf/stream

sed "s/__title__/Natale a Palm Springs/g" ./utility/kids_addStreamTemplate.json | sed "s/__url__/https:\/\/b70cb04c54ab478189e9d8ee45637b13.msvdn.net\/dom7\/podcastcdn\/teche_root\/PLAYRAI_TECHE_CINEMA_HD\/13637020_,1200,1800,2400,.mp4.csmil\/playlist.m3u8/g" | sed "s/__description__/Joe e Jessica stanno per separarsi, ma lui è ancora innamorato. I figli Dylan e Zoey fanno il tifo per il papà, però le rispettive carriere, musicista lui e pubblicitaria lei, non aiutano i suoi tentativi. Joe vorrebbe che, almeno per Natale, stessero tutti e quattro insieme, ma l'affascinante capo di Jessica, lan, le propone un importante viaggio di lavoro a Palm Springs proprio in quei giorni. Riuscirà Joe, con il prezioso aiuto di Dylan e Zoey, a riconquistare la moglie?/g" | sed "s/__year__/2014/g" | sed "s/__categories__/\"Natale-Romantico\"/g" | sed "s/__language__/Italiano/g" | sed "s/__duration__/1h17m/g" > ./outputAddStream.json
curl -k -u 1:HNVOoVhHx0yoWNIxFu-ThBA1vAPEKWsxneGgze6eoddEv9aB1xp9NzsBKDBDMEFN -d @./outputAddStream.json -H "Content-Type: application/json" https://mms-api.cibortv-mms.com/catramms/1.0.1/conf/stream
