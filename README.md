# promkit-cpp

cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j

## Config Guidelines: Single vs Mux

promkit-cpp 閺€顖涘瘮娑撱倗顫掓潻鎰攽濡€崇础閿?
- single閿涙碍鐦℃稉顏囩箻缁嬪瀚粩瀣瘹闂囪弓绔存稉?/metrics 缁旑垰褰涢敍姹竢ometheus 閻╁瓨甯撮幎鎾冲絿濮ｅ繋閲滄潻娑氣柤閵?
- mux閿涙艾鎮撴稉鈧崣鐗堟簚閸ｃ劋绮庨弳鎾苟娑撯偓娑?/metrics 缁旑垰褰涢敍鍫ｄ粵閸氬牆娅掗敍澶涚幢閸忔湹绮潻娑氣柤娴ｆ粈璐?worker 閺嗘挳婀堕張顒€婀存稉瀛樻缁旑垰褰涢敍宀€鏁遍懕姘値閸ｃ劍濮勯崣鏍ф倵閸氬牆鑻熼崘宥咁嚠婢舵牗姣氶棁灞傗偓?

閸忣剙鍙＄€涙顔岄敍鍫滆⒈缁夊秵膩瀵繘鈧氨鏁ら敍?
- exporter.host / exporter.port / exporter.path閿涙TTP 閺嗘挳婀堕崷鏉挎絻/缁旑垰褰?鐠侯垰绶為妴?
- exporter.namespace閿涙碍瀵氶弽鍥ф倳閸撳秶绱戦敍鍫濐洤 `oms` 閳?`oms_orders_total`閿涘鈧倸缂撶拋顔荤缂佸嫭婀囬崝鈥冲敶娣囨繃瀵旀稉鈧懛娣偓?
- labels 閸忋劌鐪弽鍥╊劮閿涘牊鏁為崗銉ュ煂閹碘偓閺堝瀵氶弽鍥风礆閿涙瓪service`/`component`/`env`/`version`/`instance`閵?
- buckets閿涙氨娲块弬鐟版禈濡楀爼鍘ょ純顕嗙礉闂団偓閸︺劍澧嶉張澶嬬Ч閸欏﹨顕?profile 閻ㄥ嫯绻樼粙瀣櫡娣囨繃瀵旀稉鈧懛娣偓?
- metrics閿涙艾鏁栭柌蹇撴躬闁板秶鐤嗘稉顓㈩暕鐎规矮绠熼幐鍥ㄧ垼閺冨骏绱檛ype/name/help/buckets_profile閿涘绱濋柆鍨帳鏉╂劘顢戦弮璺鸿埌閻樿埖绱撶粔浼欑幢閸斻劍鈧焦鐖ｇ粵鎹愵嚞閻劍鐏囨稉鐐煙瀵繐锛愰弰搴″帒鐠佺鈧鈧?

Single 濡€崇础閸樼喎鍨?
- 濮ｅ繋閲滄潻娑氣柤閻欘剛鐝涚紒鎴濈暰 exporter.host:port:path閿涙盯浼╅崗宥呭暱缁愪緤绱欏В蹇氱箻缁嬪顏崣锝勭瑝閸氬矉绱氶妴?
- `labels.instance` 瀵ら缚顔呴崬顖欑閺嶅洩鐦戠拠?scrape 閻╊喗鐖ｉ敍鍫濈埗閻?`host:port` 閹存牔瀵岄張鍝勬倳閿涘绱盤romQL 娑擃厼褰叉禒銉ф暏 `sum by(instance)` 缂佹潙瀹抽弶銉ュ隘閸掑棎鈧?
- `labels.component` 閸欘垳鏁ゆ禍搴㈢垼濞夈劏绻樼粙瀣潡閼硅绱欐俊?A/B閵嗕购eader/writer閿涘鈧?
- 閸氬奔绔撮張宥呭閻?`namespace`/`service`/`env`/`version` 閸︺劋绗夐崥宀冪箻缁嬪妫挎惔鏂剧箽閹镐椒绔撮懛杈剧礉娓氬じ绨懕姘値娑撳海鐡柅澶堚偓?

Mux 濡€崇础閸樼喎鍨敍鍫濆礋缁旑垰褰涙径姘崇箻缁嬪绱?
- 闁瀵岄敍姘崇殱閸忓牏绮︾€规岸鍘ょ純顔绢伂閸欙綀鐨濈亸杈ㄦЦ閼辨艾鎮庨崳顭掔幢閸忔湹绮潻娑氣柤閼奉亜濮╅梽宥囬獓娑?worker閿涘牏绮︾€?127.0.0.1 閻ㄥ嫪澶嶉弮鍓侇伂閸欙絽鑻熷▔銊ュ斀缂佹瑨浠涢崥鍫濇珤閿涘鈧?
- 閻╊喖缍嶉崣鎴犲箛閿涙orker 閸?`/tmp/promkit-mux/<namespace>` 閸愭瑥鍙嗛懛顏囬煩缁旑垳鍋ｉ幓蹇氬牚閿涙稖浠涢崥鍫濇珤閺堫剙婀撮幎鎾冲絿楠炶泛鎮庨獮韬测偓?
- 韫囧懎锝為弽鍥╊劮閿涙瓪labels.component` 韫囧懘銆忔稉鐑樼槨娑擃亣绻樼粙瀣啎缂冾喕绗夐崥宀€娈戦崐纭风礄閻劍娼甸崠鍝勫瀻娑撳秴鎮?trader/worker閿涘鈧?
- `labels.instance`閿?
  - 閹恒劏宕橀崷?mux 濡€崇础娑撳顔曠純顔昏礋閳ユ粎娴夐崥灞解偓灏栤偓婵撶礉娴狅綀銆冮懕姘値閸ｃ劌顕径鏍畱 scrape 閻╊喗鐖ｉ敍鍫濐洤 `oms-agg.local` 閹?`127.0.0.1:9464`閿涘鈧?
  - 閼辨艾鎮庣憴鍡楁禈閿涘澃um閿涘妲搁垾婊冩躬缁夊娅?component 缂佹潙瀹抽崥搴樷偓婵婄箻鐞涘瞼娈戝Ч鍌氭嫲閿涙稐璐熸禍鍡氼唨娑撳秴鎮撴潻娑氣柤閻ㄥ嫭鏆熼幑顔界湽閹鍩屾稉鈧挧鍑ょ礉閸忔湹绮崗銊ョ湰閺嶅洨顒烽敍鍫濇尐閸?`instance`閿涘绡冩惔鏂剧箽閹镐椒绔撮懛杈剧礉閸氾箑鍨导姘毉閻滅増瀵?`instance` 閸掑洤鍨庨懓灞炬￥濞夋洖鎮庨獮鍓佹畱閹懎鍠岄妴?
  - 缂佹捁顔戦敍姘躬 mux 娑撳绱滱/B 娑撱倓閲滄潻娑氣柤閻?`instance` 鐠囩兘鍘ょ純顔昏礋閻╃鎮撻崐纭风幢閹存牜娲块幒銉ф阜閻ｃ儴顕氶弽鍥╊劮閵?
- 閸忔湹绮弽鍥╊劮閿涙瓪service`/`env`/`version` 瀵ら缚顔呴崷銊﹀閺堝绻樼粙瀣╃箽閹镐椒绔撮懛娣偓鍌︾礄鐎瑰啩婊戣ぐ鍗炴惙濮瑰洦鈧崵娈戦柨顕嗙礉閼汇儰绗夐崥灞界殺鐎佃壈鍤ч弮鐘崇《閼辨艾鎮庨崚棰佺閺夆剝鈧鍣洪弮璺虹碍閵嗗偊绱?
- 閼辨艾鎮庢潏鎾冲毉缁涙牜鏆愰敍鍫濈秼閸撳秹绮拋銈忕礆閿涙瓬oth
  - 閺勫海绮忕憴鍡楁禈閿涘潷er-component閿涘绱板В蹇庨嚋鏉╂稓鈻兼稉鈧弶鈩冩鎼村骏绱濈敮?`component=<鏉╂稓鈻奸崥?`閵?
  - 閼辨艾鎮庣憴鍡楁禈閿涘澃um閿涘绱扮粔濠氭珟 `component` 閸氬孩鐪伴崪宀嬬礉鏉堟挸鍤幀濠氬櫤閺冭泛绨敍鍫滅瑝鐢?`component`閿涘鈧?
- 閻╁瓨鏌熼崶鎯ф値楠炶绱扮€佃鐦℃稉?`*_bucket`閵嗕梗*_sum`閵嗕梗*_count` 濮瑰倸鎷伴敍娑樺韫囧懐鈥樻穱婵囧閺堝绻樼粙瀣櫚閻劎娴夐崥灞俱€婇柊宥囩枂閿涘牆鎮撴稉鈧稉?`buckets_profile`閿涘鈧?

鐢瓕顫嗛梻顕€顣介敍鍦楢Q閿?
- 闂傤噯绱伴崷?mux 濡€崇础娑?`instance` 閸欘垯浜掓繅顐＄閺嶉娈戦崥妤嬬吹
  - 缁涙棑绱版稉宥勭矌閸欘垯浜掗敍宀冣偓灞肩瑬閹恒劏宕樻繅顐＄閺嶅嚖绱欓幋鏍у叡閼村棔绗夋繅顐礆閵嗗倽绻栭弽鐤粵閸氬牐顫嬮崶鐐閼宠姤濡告稉宥呮倱鏉╂稓鈻奸惃鍕殶閹诡喚婀″锝嗙湽閹粯鍨氭稉鈧弶鈩冣偓濠氬櫤閺冭泛绨敍娑樻儊閸掓瑤绱扮悮顐＄瑝閸氬瞼娈?`instance` 閸掑棜顥囬幋鎰樋閺夆剝妞傛惔蹇嬧偓?
- 闂傤噯绱伴棁鈧憰浣侯儑娑撳閲滈垾婊€绗撻梻銊ф畱閼辨艾鎮庨崳銊⑩偓婵婄箻缁嬪鎮ч敍?
  - 缁涙棑绱版稉宥夋付鐟曚降鈧倷琚辨潻娑氣柤閸︾儤娅欐稉瀣剁礉鐠嬩礁鍘涚紒鎴濈暰缁旑垰褰涚拫浣哥秼閼辨艾鎮庨崳顭掔幢閸欙缚绔存稉顏囧殰閸斻劋缍旀稉?worker閵?
- 闂傤噯绱伴懕姘値閸ｃ劏鍤滃杈╂畱閹稿洦鐖ｆ导姘愁潶閸氬牆鑻熼崥妤嬬吹
  - 缁涙棑绱版导姘モ偓鍌濅粵閸氬牆娅掓稊鐔稿瘻閼奉亣闊?`labels.component` 娴ｆ粈璐熸稉鈧稉顏佲偓娓╫rker閳ユ繂寮稉?per-component 娑?sum 閻ㄥ嫬鎮庨獮韬测偓?

PromQL 閸欏倽鈧?
- 閺勫海绮忛崥鐐叉倷閿涙瓪sum by(component) (rate(<ns>_orders_received_total[1m]))`
- 閹鍣洪崥鐐叉倷閿涙瓪sum(rate(<ns>_orders_received_total[1m]))`
- 閺勫海绮忓鎯扮箿 P95閿涙瓪histogram_quantile(0.95, sum by (le, component) (rate(<ns>_order_processing_seconds_bucket[5m])))`
- 閹鍣哄鎯扮箿 P95閿涙瓪histogram_quantile(0.95, sum by (le) (rate(<ns>_order_processing_seconds_bucket[5m])))`

缁€杞扮伐
- 閸楁洝绻樼粙瀣剁礄single閿涘绱癭examples/configs/oms_trader.toml`
- Mux 婢舵俺绻樼粙瀣剁礄娑撱倓閲?trader閿涘绱?
  - A閿涙瓪examples/configs/oms_trader_A.toml`閿涘潏omponent=`test_promkit_trader_A`閿?
  - B閿涙瓪examples/configs/oms_trader_B.toml`閿涘潏omponent=`test_promkit_trader_B`閿?
  - 閸氼垰濮╂稉銈勯嚋 `example_oms_trader` 鏉╂稓鈻奸崡鍐插讲婢跺秶骞囬敍娑滎問闂傤喛浠涢崥鍫濇珤缁旑垰褰涢弻銉ф箙閹鍣烘稉搴㈡缂佸棎鈧?

濞夈劍鍓版禍瀣€?
- 閹稿洦鐖ｈぐ銏㈠Ц闂団偓娑撯偓閼疯揪绱伴崥宥囆為妴浣鸿閸ㄥ鈧焦銆婄€规矮绠熼妴浣稿帒鐠佸摜娈戦崝銊︹偓浣圭垼缁涢箖娉﹂崥鍫濇躬閸氬嫯绻樼粙瀣付娑撯偓閼疯揪绱濋柆鍨帳閸氬牆鑻熸径杈Е閹存牕鍤悳棰佺瑝閺堢喐婀滈惃鍕彯閸╃儤鏆熼妴?
- 閹嗗厴娑撳海菙鐎规碍鈧嶇窗闁灝鍘ゆ姗€顣堕弬鏉款杻閺冭泛绨敍娑㈡閸掑墎娲块弬鐟版禈濡楄埖鏆熼柌蹇ョ礄瀵ら缚顔?<= 12閿涘绱眞orker 娑撳氦浠涢崥鍫濇珤閸︺劌鎮撴稉鈧崣鐗堟簚閸ｎ煉绱欓崣顏呭閸欐牗婀伴崷鏉挎礀閻滎垰婀撮崸鈧敍澶堚偓?

## Windows (VS2026 / MT, MTd)

- Prerequisites: Visual Studio 2026 (or 2022), CMake >= 3.22, Windows 10 SDK.
- Clone and fetch submodules (toml++ is required; prometheus-cpp optional):
  ```powershell
  git clone https://github.com/your-org/promkit-cpp.git
  cd promkit-cpp
  git submodule update --init 3rd/tomlplusplus
  # optional: git submodule update --init 3rd/prometheus-cpp
  ```
- Configure (multi-config generator, x64). Project defaults to static runtime (/MT for Release, /MTd for Debug) and static libraries; mux 鐜板凡鏀寔 Windows 骞堕粯璁ゅ惎鐢紙浣跨敤 Winsock锛夛紱椤圭洰榛樿闈欐€佽繍琛屽簱锛圧elease=/MT, Debug=/MTd锛変笖涓嶆瀯寤哄叡浜簱:
  ```powershell
  cmake -S . -B build-msvc -G "Visual Studio 18 2026" -A x64
  # 婵″倹鐏夊▽鈩冩箒 VS2026閿涘苯褰查悽?VS2022閿?-G "Visual Studio 17 2022"
  ```
- Build Debug/Release:
  ```powershell
  cmake --build build-msvc --config Debug   --parallel
  cmake --build build-msvc --config Release --parallel
  ```
- 鏉堟挸鍤敍?  - 闂堟瑦鈧礁绨遍敍?MTd,/MT閿涘绱癭build-msvc/core/<Config>/promkit-core.lib`閵嗕梗build-msvc/backends/noop/<Config>/promkit-backend-noop.lib`
  - 缁€杞扮伐閿涙瓪build-msvc/examples/<Config>/*.exe`

Notes
- prometheus-cpp 閺堫亝濯洪崣鏍ㄦ閿涘rom 閸氬海顏稉?stub閿涙稑顩ч棁鈧崥顖滄暏閿涘矁顕崗鍫濆灥婵瀵?`3rd/prometheus-cpp` 閸愬秹鍣搁弬浼村帳缂冾喓鈧?- 婵″倹鐏夐惇瀣煂 `pwsh.exe` 閻╃鍙ч惃?post-build 閹绘劗銇氶敍灞藉讲韫囩晫鏆愰幋鏍х暔鐟?PowerShell 7閵