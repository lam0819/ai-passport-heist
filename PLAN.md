# 午夜神偷 Midnight Heist — AI Passport 潛入偷寶遊戲計劃 (PLAN)

> 向 1990 年 Sega 街機《Bonanza Bros.》（香港叫《胖瘦大盜》/《龍鳳神偷》）致敬嘅**原創**潛入動作遊戲。
> 玩法靈感嚟自原作（潛入大廈、偷齊寶物、避警衛、撞門、匿埋、天台走佬），
> 但**角色、名、美術、音樂、關卡全部原創**，唔用 Sega 嘅素材、商標、角色名或者音樂。
> 目標機：AI Passport（ESP32-C3、320x240、三粒掣、冇 PSRAM），裝喺 `ota_1`，同助手共存。
> 試玩原型：`prototype/index.html`（單一 HTML 檔，瀏覽器開即玩）。

---

## 0. 資料搜集 Research（原作係點玩）

| 項目 | 原作 Bonanza Bros. | 來源 |
|---|---|---|
| 身份 | Sega 1990 街機（System 24），之後移植 Mega Drive、PC Engine、Master System、Amiga 等 | Wikipedia、arcade-history |
| 中文名 | YouTube 參考片標題「Arcade 胖瘦大盜(龍鳳神偷) 一道通關 / Bonanza Bros. 1CC」（七王的遊戲攻略 SevenWang）→ 確認即係 Bonanza Bros. | YouTube oEmbed 標題 |
| 目標 | 潛入大廈、喺時限內偷齊地圖上標示嘅寶物，再上天台搭飛船（blimp）走 | Wikipedia、Sega VC 官網、MobyGames |
| 時限 | 每關 3 分鐘（180 秒）；超時扣命兼重玩該關 | Sega VC 官網、Wikipedia、gamesrevisited |
| 動作 | 行、跳、開槍（麻醉 / 暈彈，只係暈幾秒）、閃入背景柱位 / 凹位匿埋、上落樓梯 | Wikipedia、arcade-history、HG101 |
| 撞門 | 突然開門可以撞暈門後嘅警衛；有啲關有 10 噸砝碼可以壓扁敵人；玩家自己俾門夾到都會死 | Wikipedia、HG101 |
| 警衛 | 視線有限；聽到聲或者見到你就警覺 → 匿埋 / 叫救兵 / 開槍；只會被暈幾秒 | Wikipedia、arcade-history |
| 敵人種類 | 普通警衛（瞓覺或巡邏）、資深警衛（會拉警報、只能由背後打）、防暴盾警衛（正面無敵）、廚房 / 餐飲員工（擲碟）、警犬（見到就逃）、掟炸彈嘅「硬漢」（要打幾下） | ERBE 西班牙版說明書、HG101 |
| 陷阱 | 地上嘅汽水罐、香蕉皮、耙 —— 踩到會跌低，出聲引警衛 | arcade-history |
| 中招 | 中彈 / 俾狗咬 / 俾門夾 / 跳彈床或滑索失敗 → 扣命，**身上寶物全部跌出嚟** | Wikipedia |
| 雙人 | 兩人同時合作，畫面上下分割，各自行動；中間有地圖顯示兩人同寶物位置（唔顯示敵人） | Wikipedia、HG101 |
| 關卡 | 街機 12 關（銀行、富豪大宅、賭場、造幣廠、百貨公司、金礦、珠寶店、實驗室、郵輪、古董店、美術館、金字塔）；Mega Drive 10 關 | arcade-history、HG101 |
| Bonus 關 | 單畫面 bonus：探射燈掃嚟掃去，避燈執金磚（可以匿埋 / 躲喺打開咗嘅門後）；俾燈照到即完；執晒有 10,000 分。arcade-history 話街機版每兩關一個；MobyGames 描述話每三棟大廈一個；HG101 話 Mega Drive 版冇 bonus 關（PC Engine 版有）—— 各版本唔同 | arcade-history、MobyGames、HG101 |
| 其他小細節 | 企定太耐會有烏蠅圍住你；角色似膠公仔、圓頭 | HG101 |

**冇睇過條片**：以上全部嚟自文字資料（Wikipedia、arcade-history.com、Hardcore Gaming 101、MobyGames、Sega Wii VC 官網、ERBE 說明書 PDF、gamesrevisited 評論），唔係由影片內容得出。

來源 Sources：
- https://www.youtube.com/watch?v=8KkkBvzfVbI（只讀咗標題 / 作者）
- https://en.wikipedia.org/wiki/Bonanza_Bros.
- https://www.arcade-history.com/game/310/bonanza-bros.
- http://www.hardcoregaming101.net/bonanza-bros/
- https://www.mobygames.com/game/12370/bonanza-bros/
- https://vc.sega.jp/vc_bonanza/
- https://spa2.speccy.org/Instructions/b/BonanzaBros(Erbe).pdf
- https://gamesrevisiteddotcom.wordpress.com/2017/01/19/bonanza-bros-sega-1991-sega-genesis/

---

## 1. 概念 Concept
- **你係「高佬」**：戴橙色冷帽、黑眼罩、藍白間條衫嘅瘦長神偷。拍檔「肥仔」揸住熱氣球喺天台等你。
- 每關一棟香港風味嘅大廈（大押、富豪大宅……），夜晚潛入，**偷齊寶物 → 上天台 → 熱氣球走佬**。
- 警衛有視線、會聽聲；你可以**匿埋**、用**電擊槍**射一粒發光彈暈佢（只係暈幾秒，頭頂轉星星，冇血冇傷，對小朋友友善）、或者**突然開門撞暈佢**。
- 好似 Pip's Passport 咁可以**跳**：跳過木箱、香蕉皮、警衛嘅麻醉鏢；踩**彈床**彈上一層。
- 重點係「計時 + 計路線 + 唔俾人發現」嘅緊張感，同埋撞門嘅喜劇效果。

## 2. 核心循環 Core loop
1. 關卡介紹（寶物數量、限時、命）。
2. 睇 HUD 小地圖搵寶物 → 行過就執到，寶物入袋（背囊越嚟越大）。
3. 避開 / 處理警衛：匿埋、電擊槍、撞門、跳過障礙、繞路行樓梯 / 彈床。
4. 偷齊 → 天台出口亮綠燈 → 上天台 → 熱氣球升空 → 結算（寶物 + 時間 + 冇俾人發現獎勵 → 1–3 星）。
5. 每兩關之間一個 **Bonus：金庫探射燈**（避燈執金磚）。

## 3. 三粒掣控制 Controls (3 buttons, one key at a time)
AI Passport 嘅掣係一條 ADC ladder，**一次只讀到一粒**，所以冇組合鍵。OK 一粒掣要做晒「情境動作 / 射 / 跳 / 匿埋」，
規則係睇**你郁緊定企定**同**撳幾耐**（同 Pip's Passport 一樣：放開方向掣再撳 OK，速度會帶入跳）：

| 情況 | 撳法 | 結果 |
|---|---|---|
| 行緊（\|vx\| ≥ 20 px/s，或者放開方向掣 < 0.15 s 仲滑緊） | 撳 OK | **即刻跳**，保留行嘅速度（**跑先 = 跳得遠**）；撳住 = 上升時重力細啲（**撳耐 = 跳得高**，最高 21 px，唔撞天花） |
| 企定，喺出口 / 樓梯 / 門前 | 短按 OK（≤ 0.18 s） | 情境動作，優先：**天台出口**（偷齊先得）→ **樓梯** → **門**（開 / 閂；門後有警衛就撞暈佢） |
| 企定，其他位 | 短按 OK | **電擊槍**：向面向方向射一粒慢慢飛嘅藍色光彈（220 px/s、射程 130 px、冷卻 0.6 s） |
| 企定，喺柱位 / 凹位 | 撳住 OK | **匿埋**（即刻），放手出返嚟 |
| 企定，其他位（包括樓梯 / 門前） | 撳住 OK > 0.18 s | **原地跳**（照樣撳耐跳高） |
| 半空 | UP / DOWN | 微調方向（空中加速度細啲） |
| 撳住 UP / DOWN | — | 向左 / 向右行（有加速同滑停；可喺選單 KEYS 對調） |
| 向牆（或閂咗嘅門 / 關卡盡頭）一直行 1.5 秒 | — | **暫停**（wall-push，有 PAUSE 條；半空唔計） |

- 點解唔用 double-tap：要等第二下會令每個短按慢 0.2–0.3 s，射擊同開門會覺得「黐」；「郁緊 = 跳、企定 = 射」即時反應，又同 Pip 嘅手感一致。
- 行緊經過樓梯 / 門口想用情境動作：停低（放手等滑停）再短按。行緊撳 OK 永遠係跳。
- 撞門要**面向門**；門向你面向嘅方向打開。閂咗嘅門擋住人、擋住視線、擋住光彈同麻醉鏢。
- 原型喺瀏覽器額外接受 UP+DOWN 一齊撳做暫停（只係方便測試，機上做唔到）。
- 選單：UP/DOWN 揀、OK 確定；暫停選單：繼續 / 重玩 / 聲音 / 掣位 / 返標題 / 返助手。

## 4. 機制 Mechanics

### 4.1 大廈 Building
- 側面睇，**最多 3 層，每層 64 px**，成棟大廈高度一次睇晒；左右捲動（每關 2–3 個畫面闊，32–44 格 x 16 px）。
- 2 層嘅關卡會將地下貼住畫面底，上面多咗天空（熱氣球同提示用）。
- 地圖字元：`.` 走廊、`S` 起點、`E` 天台出口、`H` 匿埋凹位、`|` 門、`$` 寶物、`^`/`v` 樓梯（上 / 落，要同一 column 配對）、`g` 警衛、`G` 盾牌警衛、`z` 瞓覺警衛、`d` 警犬、`c` 汽水罐、`x` 木箱（14 px 高，擋路，要跳過或者跳上去）、`p` 香蕉皮（行過會跣 0.7 s 兼好大聲，跳過就冇事）、`b` 彈床（踩到即刻彈上一層，穿過上一層同一 column 嘅天窗 `o`）。
- 關卡生成器（之後嘅 `gen_levels.py`）會驗證：闊度、樓梯配對、彈床同天窗配對、木箱唔可以貼住樓梯 / 門 / 柱位 / 彈床、每件寶物同出口都去得到（樓梯同彈床都計）、數量上限。原型已經喺 `build()` 做咗同樣檢查。

### 4.1b 跳 Jump（同 Pip's Passport 一樣嘅手感）
- 起跳 120 px/s，重力 520 px/s²；撳住 OK 頭 0.25 s 上升時重力減到 220 → 短按跳 ~14 px、撳住 ~21 px（頂到天花就停）。
- 跑先：保留行速（70 px/s），一跳大約 28–40 px 遠；企定原地跳就只係垂直。
- 半空撞到木箱側面唔會即刻停晒速度，等遲少少起跳都可以「爬」上箱頂。
- 半空唔可以匿埋、唔可以開門 / 上樓；唔會踢到罐、唔會執寶物（要落返地）。
- 麻醉鏢飛喺膝頭高度（12 px），撳住跳可以跳過；警衛同狗：跳到夠高（狗 12 px）可以避過狗咬。
- 彈床：踩上去即刻彈（280 px/s），穿過天窗落喺上一層；彈緊嗰陣警衛睇唔到你。

### 4.2 警衛 AI Guards（細 state machine，每隻 ~32 bytes）
| 狀態 | 行為 |
|---|---|
| `patrol` 巡邏 | 喺出生點左右 5 格之間行，到盡頭停 1.2 s 再轉身；撞到閂咗嘅門就轉頭 |
| `sleep` 瞓覺 | 坐喺凳度瞓；你喺佢身邊逗留 > 1.2 s 或者附近有聲 → 醒 |
| `investigate` 查看 | 行去聲源 / 最後見到你嘅位置，左右望 2.5 s，再返去巡邏（會自己開門） |
| `alert` 警覺 | 「!」；追你、保持 ~56 px 距離、蓄力 0.5 s 射麻醉彈（每 1.5 s）；失去視線 3 s 就放棄 |
| `stunned` 暈 | 頭頂轉星星；電擊槍 5 s、撞門 7 s、狗 3 s；之後變 `investigate` |

- **視線**：同一層、面向方向、100 px（狗 84 px）；閂門擋視線；你匿埋時睇唔到；企喺佢背後貼身都會被發現。
- **發現條**：見到你時黃條漸滿（0.45 s，近距離快一倍），滿咗先 alert —— 俾玩家半秒反應時間匿埋或射。
- **盾牌警衛 `G`**：光彈打中正面盾牌會彈開（「擋！」）兼即刻 alert；要由背後射或者撞門。
- **警犬 `d`**：行得快，見到你就衝（92 px/s），咬到即扣命；電擊槍只暈 3 s。
- **聲音**：踢到汽水罐（110 px）、踩到香蕉皮（110 px）、光彈落點「啪」一聲（56 px，槍口本身好靜）、撞門（90 px）會令同層附近警衛過嚟查看。
- 木箱對警衛係牆（巡邏去到會轉頭）；光彈撞到木箱會停。

### 4.3 寶物、中招、時限 Loot, getting caught, timer
- 行過寶物即執；HUD 顯示 `寶物 n/N`，小地圖顯示未執嘅寶物（黃點）同出口（紅 / 綠）。
- **中招**（中彈、俾狗咬、俾警衛捉、俾警衛開門撞到）：扣 1 命，跌低 1.1 s，**只跌最後執嗰一件寶物**（跌喺身後），之後 2.2 s 無敵。
- 每關有自己嘅時限（135–160 秒；bonus 30–42 秒），開關卡片會顯示。超時：扣 1 命兼重玩該關。3 條命，冇命 = GAME OVER。

### 4.4 Bonus：金庫探射燈
- 單一畫面（20 格闊）、3 層、樓梯；每層一盞探射燈左右掃。
- 時限內執晒金磚（每舊 300），全部執晒 +5000；俾燈照到（冇匿埋）即完。
- Bonus 唔扣命；完咗（執晒或者夠鐘）就開下一關。星：全部 ★★★、≥ 2/3 ★★、其他 ★。

### 4.5 關卡 Levels（12 關 + 4 bonus，一定要順序玩）
每三關之後有一個 bonus。全部原創香港地點；難度逐關加（層數、警衛、狗、盾牌、罐 / 香蕉皮、彈床、時限），由 `gen_levels.py` 檢查唔會跌返落嚟。

| # | 關卡 | 層 × 格 | 寶物 | 警衛 / 盾 / 狗 / 瞓 | 門 / 箱 / 罐 / 蕉皮 / 彈床 | 時限 |
|---|---|---|---|---|---|---|
| 1 | 1-1 大押 Pawnshop（教學） | 2 × 32 | 3 | 2/0/0/1 | 2/1/1/0/1 | 150 |
| 2 | 1-2 茶樓 Tea Hall | 2 × 38 | 4 | 2/0/0/1 | 2/1/2/0/0 | 150 |
| 3 | 1-3 機舖 Arcade（第一隻狗） | 3 × 40 | 4 | 2/0/1/1 | 3/2/2/1/1 | 150 |
| 4 | Bonus 金庫探射燈 | 3 × 20 | 8 金磚 | 3 盞燈 | — | 30 |
| 5 | 2-1 參茸海味舖 Ginseng（第一個盾牌） | 3 × 42 | 5 | 2/1/0/2 | 3/2/2/1/0 | 150 |
| 6 | 2-2 電車廠 Tram Depot（兩張彈床） | 3 × 46 | 5 | 4/1/1/0 | 3/2/1/2/2 | 150 |
| 7 | 2-3 富豪大宅 Mansion | 3 × 44 | 5 | 3/1/1/1 | 5/3/3/2/1 | 135 |
| 8 | Bonus 玉器市場 Jade Market | 3 × 26 | 8 金磚 | 3 盞燈 | 1 箱 | 30 |
| 9 | 3-1 渡輪碼頭 Ferry Pier | 3 × 50 | 6 | 4/1/1/1 | 4/2/2/2/0 | 150 |
| 10 | 3-2 洋服店 Tailor | 3 × 52 | 6 | 4/2/2/1 | 4/2/2/1/1 | 140 |
| 11 | 3-3 金舖 Goldsmith（第一棟 4 層） | 4 × 44 | 7 | 4/2/1/1 | 4/3/2/2/0 | 140 |
| 12 | Bonus 帆船貨艙 Junk | 3 × 30 | 9 金磚 | 3 盞燈 | 1 箱 | 35 |
| 13 | 4-1 海景酒店 Harbour Hotel | 4 × 50 | 8 | 5/2/1/1 | 5/3/2/1/1 | 150 |
| 14 | 4-2 博物館 Museum | 4 × 56 | 9 | 5/2/2/1 | 6/3/2/2/1 | 150 |
| 15 | 4-3 銀行大廈 Bank Tower（大結局） | 4 × 60 | 10 | 6/3/2/1 | 7/3/2/2/1 | 160 |
| 16 | Bonus 山頂寶庫 Peak Vault | 4 × 28 | 10 金磚 | 4 盞燈 | — | 42 |

- **4 層**：畫面只放得落 3 層 + 天台，鏡頭跟住高佬上下滑（`world_view_y0`）；2–3 層嘅關卡畫面同以前一樣。
- **解鎖 / 進度**：打完第 n 關先開第 n+1 關；選關畫面顯示鎖住嘅關（灰色 LOCKED），揀咗會話要先過邊關。NVS `heist` 嘅 blob `progress`（版本、最遠解鎖、每關最佳星數）重開機都喺度。選單最底「RESET PROGRESS」要撳兩次 OK（郁游標即取消），最高分保留。
- **檢查器** `tools/gen_levels.py`：陣列上限、樓梯 / 彈床對位、木箱要有助跑位同離樓梯 / 門 ≥ 3 格；路線圖（行、門、跳、樓梯、彈床）確保全部寶物同出口去得到，用 Held-Karp 計最短路線；時間壓力（路線 + 每隻警衛額外時間）≤ 85%（bonus 80%）；威脅值同時間壓力逐關唔准跌。
- **自動駕駛**：host harness 用真嘅按掣邏輯逐關打通（警衛暈住、燈熄咗），證明每關真係玩得完。

## 5. 計分 Scoring
| 項目 | 分數 |
|---|---|
| 寶物 | 每件 800–2500（按種類：金條、手錶、玉、花瓶、名畫、珠寶） |
| 電擊槍暈警衛 | +100 |
| 撞門撞暈 | +300 |
| 剩餘時間 | 每秒 +20 |
| 全關冇俾人發現 Ghost | +3000 |
| Bonus 金磚 | 每舊 300，全部 +5000 |
| 星級 | ★ 走得甩；★★ 剩 ≥ 40% 時間；★★★ 再加冇俾人發現 |

最高分、最遠解鎖關卡同每關最佳星數存 NVS（`heist` / `progress`）。

## 6. 美術 / 聲音 Art & audio（全部原創、程序生成）
- **美術**：`tools/gen_assets.py` 由長方形 / 小 pixel map 生成：高佬（12x37，手持黃色玩具款電擊槍）、肥仔、熱氣球、警衛 3 款、狗、門、柱位、樓梯、寶物 6 款、罐、木箱、香蕉皮、彈床、天窗。
  機上版本用 runtime 長方形 primitive 畫（同原型 `R()` 一一對應），唔使 sprite bitmap，慳 flash 同 RAM。
  夜景配色（深藍牆、月光窗、燈光圈），每關一套 palette。
- **唔抄原作**：唔用 Robo / Mobo、飛船造型、原作音樂或者畫面；只保留「潛入偷寶」呢個類型嘅共通元素。
- **聲音**：ES8311 synth —— 偷偷摸摸嘅 walking bass loop（128 bpm，有警衛警覺或者 < 30 s 時加快到 168 bpm）；效果音：電擊槍「嗞」、跳、落地、彈床「boing」、跣親、撞門、開門、罐聲、警報、射擊、中招、執寶、樓梯、計時嘀嘀聲、走佬 fanfare。

## 7. 技術架構 Tech architecture

### 7.1 硬件 / 平台限制（同 Pip 同消防小隊一樣）
- ESP32-C3、160 MHz、**冇 PSRAM**、冇 FPU；Wi-Fi / BT 唔開（純離線）。
- **冇 full framebuffer**：6 條 40 行 band，2 個 25 KB static buffer 輪流 DMA。
- 掣 GPIO0 ADC ladder，一次讀一粒。
- 遊戲 loop **唔做任何 malloc**；所有 state 固定大細陣列；美術、關卡 `const` 放 flash。
- **固定 1/30 s timestep**（最多追 3 步），原型亦用同一個 loop。

### 7.2 記憶體同幀率重點 Memory & frame rate
- 關卡唔使 tile grid：每層只係一行物件（門、柱、樓梯、寶物、警衛、罐），用細 struct 陣列（`uint16 x; uint8 floor, kind`）。
- 背景（牆、間條、窗、燈圈）每條 band 用 fill 畫，唔使 bitmap；燈光圈 / 探射燈用預先混好嘅顏色（唔做逐 pixel alpha blend）。
- 只畫 band 有接觸嘅物件（同消防小隊一樣嘅 band early-out）。
- 暫停 / 結算畫面只喺有變先重畫。
- 預計 per-frame：背景 fill ~77k px + 物件 < 10k px → 遠低過 SPI 傳送時間（40 MHz ~31 ms / frame）。

### 7.3 同助手共存 Sharing the board
- 用助手嘅 partition table（offset 0x10000）；裝 **`ota_1` @ 0x400000**（同 Pip / 消防小隊爭同一個 slot，見 §11）。
- 同一條 `dev_signing_key.pem` sign（唔 commit）；`CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y`，開機 `boot_mark_valid()`。
- **NVS namespace：`heist`**（`hiscore`、`stars0..N`、`sound`、`swapkeys`）；共用 NVS，永遠唔 erase。
- 只用 `tools/flash.sh`；**唔可以 `idf.py flash`**。
- 社區發佈要 0x0 嘅完整 merged image（會取代助手）—— 同之前兩隻一樣，要喺說明寫清楚。

### 7.4 重用 Reuse（由 ai-passport-firefighter copy，唔改原 repo）
| 模組 | 處理 |
|---|---|
| `board.h`、`display.c/.h`、`input.c/.h`、`boot.c/.h`、`main.c` | 照搬 |
| `gfx.c/.h` | 照搬（已有 band early-out、`gfx_mix`） |
| `sound.c/.h` | 照搬 synth，換效果音同音樂 |
| `game.c` state machine、wall-push 暫停、fixed timestep、NVS、選單 | 參考 / 改寫 |
| `tools/flash.sh`、`tools/shots/` | 照搬架構 |
| `building/civilians/water/firefighter` | **重寫**成 `level.c`、`guards.c`、`thief.c`、`loot.c` |

## 8. 檔案結構 File layout（實際）
```
ai-passport-heist/
├── PLAN.md  README.md  .gitignore
├── prototype/index.html      瀏覽器原型
├── prototype/levels.js       gen_levels.py 生成（同 firmware 同一套地圖）
├── CMakeLists.txt            project(ai-passport-heist)
├── partitions.csv  sdkconfig.defaults
├── main/
│   ├── main.c board.h boot.* display.* input.* gfx.*   (由消防小隊 copy；gfx 加咗 gfx_band_begin_raw)
│   ├── sound.*               synth 照搬，換晒效果音同原創音樂
│   ├── config.h              所有 tunables（同原型 C 物件一一對應）
│   ├── world.c/.h            關卡物件、高佬（行 / 跳 / 匿埋 / 電擊槍 / 中招）、警衛 AI、光彈、麻醉鏢
│   ├── art.c/.h              程序美術（長方形）、HUD、小地圖
│   ├── game.c/.h             states、選單、提示、計分、NVS `heist`、暫停、fixed timestep
│   ├── levels.c/.h           gen_levels.py 生成
│   └── assets.c/.h           gen_assets.py 生成（只係字型）
└── tools/ flash.sh  gen_levels.py  gen_assets.py  proto_test.sh/.js  shots/
```

## 9. 記憶體預算 Memory budget（實際 build，ESP-IDF v6.0.1）
| 項目 | 數字 |
|---|---|
| Band buffers 2 x 320x40x2 | 50 KB |
| `world_t`（成個關卡 + 高佬 + 警衛 + 光彈 + 鏢 + 字；最大 4 層 × 64 格、12 警衛） | 1.7 KB |
| 遊戲 code（libmain） | 43 KB flash；data 3.8 KB（包埋 16 關地圖） |
| Static DRAM 總數 | 65.8 KB（.bss 58.7 + .data 7.1） |
| IRAM | 52.8 KB |
| 剩餘 heap + stacks | ~198 KB |
| 實機（2026-10-10，標題畫面） | 30.0 fps、0 late、free heap 251 KB；標題一幀 31–36 ms，所以標題 15 fps 重畫（撳掣即刻重畫），邏輯照 30 Hz |
| 繁忙一幀 | ~152k pixel writes（4 層銀行大廈 12 警衛 ~147k；bonus ~176k）；SPI 40 MHz 送一幀 ~31 ms 先係瓶頸 |

## 10. 里程碑 Milestones
1. **M0** 原型（已完成）：`prototype/index.html`，2 關 + 1 bonus，跳 / 電擊槍 / 彈床 / 木箱 / 香蕉皮，headless 測試 50 項通過。
2. **M1–M5**（已完成，未上機）：ESP-IDF v6 firmware，46 項 host 規則檢查通過，build 冇 warning。
3. **M2** 玩家 + 關卡物件：行、樓梯、門、匿埋、寶物、出口、HUD + 小地圖。
4. **M3** 警衛 AI：巡邏 / 瞓覺 / 查看 / 警覺 / 暈、視線、聲音、麻醉彈、狗、盾牌。
5. **M4** Bonus 探射燈、計分、星級、結算。
6. **M5** 美術 / 聲音生成器、`tools/shots` 截圖 + 規則檢查。
7. **M6** 關卡擴展（2026-10-10 完成）：12 關 + 4 bonus、順序解鎖、NVS 進度 + reset、4 層鏡頭、檢查器 + 自動駕駛；host 97 項、原型 79 項通過。
8. **M7** 實機玩一輪量遊戲中 fps、調時限同跳手感。

## 11. 已決定 Decisions（2026-10-09）
1. 名稱：「午夜神偷 Midnight Heist」，主角「高佬」、拍檔「肥仔」；folder 改名 `ai-passport-heist`。
2. 武器：**電擊槍**（只係暈，光彈 + 星星，冇血）代替閃光相機。
3. 跳：跟 Pip's Passport（撳耐跳高、跑先跳遠）；單掣方案見 §3；加返彈床、木箱、香蕉皮。
4. 中招只跌**一件**寶物（最後執嗰件）。
5.（2026-10-10）12 關 + 每 3 關一個 bonus，一定要順序解鎖；bonus 唔扣命、完咗就開下一關；reset 唔清最高分；舊版 `stars0..7` 星數唔搬過嚟（關卡次序已經改咗）。
6. 富豪大宅地下嘅木箱由第 14 格搬去第 13 格（門隔籬冇位助跑）。

## 12. 未解決問題 Open questions
1. **雙人**：一部機做唔到分畫面雙人。之後可唔可以兩部 AI Passport 用 ESP-NOW 連線合作（一人一部）？會用到 Wi-Fi radio 同 RAM，要另外評估。
2. **時限**：4-3 銀行大廈（自動駕駛 115 / 160 秒）同後期 bonus 比較緊，要實機試。
3. **`ota_1` 衝突**：第三隻遊戲同樣要 `ota_1`；係咪要考慮助手加「遊戲選單」+ 細啲嘅多個 app slot？
4. **中文字**：機上冇 CJK 字型；HUD / 提示暫時用英文。
5. **跳手感**：重力 / 起跳速度要實機試（ADC 讀掣有 ~20 ms 延遲）。
