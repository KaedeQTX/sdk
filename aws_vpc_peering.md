# AWS 東京 (ap-northeast-1, AZ ID: apne1-az4) – VPC 建立、跨帳號 VPC Peering、Shared Cluster Placement Group 與在 Peered VPC 下開機器（Console 版 Step-by-Step）

最後更新：2025-10-08

> 本教學以 **AWS Management Console（網頁版）** 操作為主，區域使用 **東京 `ap-northeast-1`**，並指定 **Availability Zone ID `apne1-az4`**（注意：每個帳號的 `1a/1c/1d` 字母對應不同，**請以 AZ _ID_** 對齊跨帳號與跨 VPC 的實際實體位置）。
>
> 目標：
>
> 1. 建立或確認 VPC 與子網（位於 `apne1-az4`）；
> 2. 與另一個 AWS 帳號建立 **VPC Peering**（對方發起，我方提供必要 ID、我方於 Console 接受並雙邊更新 Route Table / DNS）；
> 3. 由對方透過 **AWS RAM** 分享 **Cluster Placement Group（CPG）**（Shared Placement Group）；
> 4. 我方在「**己方 VPC（與對方 VPC 已 Peering）** + **Shared CPG** + **同一 AZ ID (`apne1-az4`)**」下，於 Console 開 EC2 實例。

---

## 0. 前置檢查與名詞釐清

-   **Region**：東京 `ap-northeast-1`（下稱 _apne1_）。
-   **AZ ID**：`apne1-az4`（**不是** `ap-northeast-1a/1c/1d` 這種字母代碼；不同帳號的字母 → 實體機房對應不同）。
-   **VPC Peering**：兩個 VPC（可跨帳號）之間的路由連接；**CIDR 不可重疊**；建立後需**雙邊**更新 Route Tables 與（可選）DNS 解析。
-   **Shared Cluster Placement Group（CPG）**：透過 **AWS RAM** 將 **Cluster** 型態的 Placement Group 分享給其他帳號。**Cluster PG 可以在同一 Region 內、跨「已 Peering 的 VPC」共同使用**，但仍受 **單一 AZ** 限制；要對齊 **同一 AZ ID** 才能享有同機架/低延遲好處。

### QTX 各區域使用的 VPC CIDR

QTX 在不同 AWS 區域使用以下 CIDR 範圍，請確保您的 VPC CIDR **不與這些範圍重疊**：

| 區域             | Region Code      | QTX VPC CIDR     |
| ---------------- | ---------------- | ---------------- |
| **日本（東京）** | `ap-northeast-1` | `10.11.0.0/16`   |
| **香港**         | `ap-east-1`      | `192.168.0.0/16` |
| **新加坡**       | `ap-southeast-1` | `172.31.0.0/16`  |

> **重要提醒**：
>
> -   建立 VPC 時，請選擇**不與上述 QTX CIDR 重疊**的範圍。
> -   **以下 CIDR 不能使用**（因為 QTX 服務已在使用，雖然您不會直接連接到這些網段）：
>     -   `172.15.0.0/16`
>     -   `10.18.0.0/16`
> -   建議使用：`10.20.0.0/16`、`10.30.0.0/16`、`172.16.0.0/16`、`172.17.0.0/16` 等。
> -   設定 Security Group 時，請依照您所連接的 QTX 區域，使用對應的 CIDR 作為來源。

---

## 1. 建立（或確認）VPC 與子網（位於 `apne1-az4`）

> 若已有可用 VPC，可跳到 **2. 準備跨帳號 VPC Peering**，但務必先確認：
>
> -   VPC 的 **IPv4 CIDR** 與對方 VPC **不重疊**；
> -   至少有一個子網的 **AZ ID = `apne1-az4`**；
> -   路由表規劃（公/私子網）無誤。

### 1.1 用 Console 新建 VPC（簡易）

1. 進入 **VPC Console** → 左側選單 **Your VPCs** → **Create VPC**。
2. **Resources to create**：

    - 若要一次建立 VPC+子網+IGW+Route Table，選 **VPC and more**；
    - 只想先建空 VPC，選 **VPC only**。

3. 指定 **IPv4 CIDR**（例：`10.20.0.0/16` 或 `172.16.0.0/16`，請預先與 QTX 協調，避免與 QTX VPC CIDR 重疊。**注意**：以下範圍**不能使用**：`10.11.x.x`、`10.18.x.x`、`172.15.x.x`、`172.31.x.x`、`192.168.x.x`）。
4. 建立完成後，於 **Your VPCs** 檢視 **DNS hostnames / DNS resolution** 是否 **Enabled**（若需要訪問外網時會用到）。

### 1.2 建立或確認子網位於 `apne1-az4`

1. **Subnets** → **Create subnet** → 選你的 VPC。
2. **Availability Zone** 下拉會顯示字母（如 `ap-northeast-1d`），但請在子網詳細頁的欄位中確認 **Availability Zone ID** 是否為 **`apne1-az4`**。
3. 指定子網 CIDR（例：`10.20.4.0/24`）。
4. 建好後到子網詳情頁，加入必要的 **Route Table** 與 **Network ACL** 設定（公子網請在 Route Table 將 `0.0.0.0/0` 指到 IGW；私子網視需要經 NAT Gateway）。

> **外網連線提醒**：
>
> 如果您的 EC2 實例需要訪問**外部網站**（如下載套件、API 呼叫等），請確保：
>
> -   **公有子網（Public Subnet）**：
>     -   子網的 Route Table 包含 `0.0.0.0/0 → Internet Gateway (igw-...)`
>     -   實例需要有**公有 IP** 或 **Elastic IP**
>     -   VPC 的 **DNS resolution** 與 **DNS hostnames** 需啟用
> -   **私有子網（Private Subnet）**：
>     -   子網的 Route Table 包含 `0.0.0.0/0 → NAT Gateway (nat-...)`
>     -   NAT Gateway 必須位於公有子網中
>     -   VPC 的 **DNS resolution** 需啟用
>
> **注意**：與 QTX 的 VPC Peering 通訊**不需要外網連線**，僅使用私有 IP。上述設定僅在您需要訪問外部網際網路時才必要。

> **如何查 AZ ID**：
>
> -   **VPC → Subnets** 詳情面板有 **Availability Zone ID**；
> -   **EC2 Dashboard** 右側資訊窗或 **AWS RAM Console** 也能看到 **Your AZ IDs**。
>
> 跨帳號合作時，請**雙方**以 AZ **ID** 溝通（如 `apne1-az4`），而不是 `1a/1c/1d`。

---

## 2. 準備跨帳號 VPC Peering（對方發起）

### 2.1 你需要提供給對方的資訊（給「發起 Peering 的對方帳號」）

-   **你的 AWS Account ID**（12 位數）。
-   **你的 VPC ID**（格式 `vpc-xxxxxxxxxxxxxxxxx`）。
-   **你的 VPC 所在 Region**：`ap-northeast-1`。
-   **你的 VPC IPv4 CIDR**（例：`10.20.0.0/16`）。
-   （若要享有 Shared CPG 的低延遲放置）**預計對齊的 AZ ID**：`apne1-az4`（雙方各自準備在該 AZ ID 的子網）。

> **注意**：兩邊 VPC 的 **CIDR 不可重疊**，否則 Peering 無法正確路由。

### 2.2 對方在其帳號的 Console 發起 Peering（摘要）

-   **VPC Console → Peering connections → Create peering connection**：

    -   **Requester**：對方自己的 VPC；
    -   **Accepter**：選 **Another account**，輸入你的 **Account ID**，並選擇你的 VPC（或填你的 VPC ID）；
    -   確認 Region（同區 `ap-northeast-1`）。

### 2.3 你在自己的帳號 Console 接受 Peering

1. **VPC Console → Peering connections**，找到狀態 `pending-acceptance` 的連線。
2. **Actions → Accept request**。
3. 之後狀態應為 `active`。

### 2.4 雙邊更新 Route Tables 與 Security Groups

1. **雙方**到 **VPC → Route tables**，在「會互通的子網對應的 Route Table」加一條：

    - **Destination**：QTX VPC 的 **CIDR**（例：`10.11.0.0/16`）；
    - **Target**：剛建立的 **Peering Connection（pcx-...）**。

2. **安全群組（SG）/ NACL**：開放彼此所需的埠號與來源 CIDR（或內部約定的範圍），否則雖有路由仍無法通。

> **重要提醒**：
>
> QTX 服務使用 **私有 IP 位址**（即 VPC CIDR 範圍內的 IP）進行通訊，不使用公有 IP 或 DNS 名稱。
>
> -   所有與 QTX 的連線都透過 **VPC Peering** 使用私有 IP（依區域而定，如東京為 `10.11.x.x`、香港為 `192.168.x.x`、新加坡為 `172.31.x.x`）。
> -   請確保您的應用程式配置使用 QTX 提供的**私有 IP 位址**進行連接。
> -   不需要設定 DNS 解析或使用主機名稱。

    > **重要 – Security Group 設定要求（來自 QTX）**：
    >
    > QTX 需要您在**您的 Security Group** 中開放以下規則，來源設為 **QTX VPC 的 CIDR**（請依照您連接的區域選擇對應 CIDR，參見第 0 節）：
    >
    > - **ICMP（所有類型）**：用於測試連線與網路診斷（如 `ping`）。
    >
    >     - **Type**: All ICMP - IPv4
    >     - **Source**: QTX VPC CIDR（依區域而定）
    >
    > - **UDP（所有埠）**：QTX 需要使用所有 UDP 連線進行資料傳輸。
    >     - **Type**: Custom UDP
    >     - **Port range**: 0 - 65535（或填 `0` 表示所有埠）
    >     - **Source**: QTX VPC CIDR（依區域而定）
    >
    > **範例（連接到東京區域的 QTX，您的 VPC 使用 `10.20.0.0/16`）**：
    >
    > | Type            | Protocol | Port Range | Source       | Description              |
    > | --------------- | -------- | ---------- | ------------ | ------------------------ |
    > | All ICMP - IPv4 | ICMP     | N/A        | 10.11.0.0/16 | Allow ICMP from QTX Tokyo|
    > | Custom UDP      | UDP      | 0 - 65535  | 10.11.0.0/16 | Allow all UDP from QTX Tokyo|
    >
    > **各區域 QTX CIDR 對照表**：
    >
    > | 連接區域                     | QTX VPC CIDR     |
    > | ---------------------------- | ---------------- |
    > | 日本（東京）`ap-northeast-1` | `10.11.0.0/16`   |
    > | 香港 `ap-east-1`             | `192.168.0.0/16` |
    > | 新加坡 `ap-southeast-1`      | `172.31.0.0/16`  |
    >
    > 請注意：您的 VPC 請使用**不與 QTX 重疊**的 CIDR（如 `10.20.0.0/16`、`10.30.0.0/16`、`172.16.0.0/16`、`172.17.0.0/16` 等）。

---

## 3. 讓對方分享 Shared **Cluster** Placement Group（CPG）並接受邀請

> 只有 **Cluster** 類型的 Placement Group 才能在**同一 Region**、**跨「已 Peering」的 VPC** 共同使用；而且**必須在同一個 AZ（以 AZ ID 對齊，例如 `apne1-az4`）**。

### 3.1 你需要提供給對方的資訊（給「分享 CPG 的對方帳號」）

-   你的 **AWS Account ID**（作為 AWS RAM 的 _principal_）。
-   預計對齊的 **AZ ID**：`apne1-az4`（雙方子網都在此 AZ ID）。
-   （選配）若對方使用 **Capacity Reservation in CPG（ODCR-C、CR in CPG）**，可請對方一併透過 RAM 分享相應的 **Capacity Reservation**。

### 3.2 對方在其帳號分享 CPG（對方動作摘要）

-   進入 **EC2 → Placement groups**，確認 PG **策略為 Cluster**；
-   進入 **AWS RAM → Create resource share**：新增資源 **Placement group**，將你的 **Account ID** 加入 _Principals_；送出。

### 3.3 你在自己的帳號接受分享

-   **AWS RAM Console → Shared with me**，找到 **Placement group** 的分享 → **Accept**。
-   接受後，**EC2 → Placement groups** 下拉就能選到對方分享的 **CPG 名稱**。

> 小提醒：**CPG + VPC Peering + 同 AZ ID** 三要素都到位，跨帳號、各自 VPC 的實例才會在底層被「極度靠近地放置」，達到低延遲高吞吐的效果。

---

## 4. 在「己方 VPC（已 Peering）」+「Shared CPG」+「`apne1-az4` 子網」下開 EC2 機器（Console）

1. 進入 **EC2 Console → Instances → Launch instance**。
2. 選擇 AMI / Instance type（建議支援 **Enhanced Networking** 的機型，如 `c7i/c8g` 等）。
3. **Network settings**：

    - **VPC**：選**你自己的 VPC**（即與對方 VPC 已 Peering 的那個 VPC）；
    - **Subnet**：選 **位於 `apne1-az4`** 的子網（請看子網詳情中的 **Availability Zone ID** 欄位核對）；
    - （安全群組）加入能與對方實例互通所需的規則（來源可先以對方 VPC CIDR 限定）。

4. **Advanced details → Placement group**：

    - **Placement group**：從下拉選對方分享過來的 **Cluster Placement Group 名稱**；
    - **Capacity Reservation（若對方有分享 CR in CPG）**：依需求指派。

5. 建立後在 **Instances** 清單的欄位中可看到實例的 **Placement group** 名稱與 **AZ**。

> 若下拉沒有看到對方的 CPG：確定已在 **AWS RAM → Shared with me** 接受、Region 一致、PG 策略是 **Cluster**、以及 VPC Peering 已建立；同時你所選的 **子網 AZ ID** 必須與對方實例所在 CPG 的 AZ ID 相同（例如皆為 `apne1-az4`）。

---

## 5. 驗證與健診

-   **路由**：在兩邊實例互相 `ping` / `traceroute` 私有 IP（或以自家 UDP/TCP 偵測）；若不通，先檢查 **Route Table** 是否加入對方 CIDR → **Target = Peering (pcx-...)**。
-   **安全群組 / NACL**：逐項檢查是否擋到所需連線埠與方向。
    -   **特別注意**：確認已依照 QTX 要求開啟 **ICMP**（測試連線）與 **所有 UDP 連線**（port 0-65535），來源為 QTX VPC 的 CIDR。
-   **外網連線**（若需要）：若實例需要訪問外部網站，確認：
    -   公有子網：Route Table 有 `0.0.0.0/0 → IGW`、實例有公有 IP、VPC DNS 已啟用
    -   私有子網：Route Table 有 `0.0.0.0/0 → NAT Gateway`、VPC DNS resolution 已啟用
-   **放置狀態**：在 **EC2 → Instances** 檢查 **Placement group** 欄位；必要時可用 **Actions → Instance settings → Modify instance placement** 嘗試移入/更換 PG（但受容量限制）。
-   **AZ ID 對齊**：確認雙方實例/子網的 **AZ ID = `apne1-az4`**。

---

## 6. 常見錯誤與排查清單

-   **CIDR 重疊**：更換一方的 VPC CIDR（或改用第二個 VPC）。請確保您的 VPC **不使用** QTX 的 CIDR（東京 `10.11.0.0/16`、香港 `192.168.0.0/16`、新加坡 `172.31.0.0/16`）以及 `172.15.0.0/16`、`10.18.0.0/16`（QTX 服務已在使用）。建議使用 `10.20.0.0/16`、`10.30.0.0/16`、`172.16.0.0/16`、`172.17.0.0/16` 等。
-   **忘記雙邊加 Route**：Peering 成功但仍不通，通常是未在子網對應的 Route Table 新增 `對方CIDR → pcx-...`。
-   **安全群組阻擋**：請以 QTX VPC CIDR 開放必要埠號。
    -   **特別提醒**：務必依照 QTX 要求開啟 **ICMP**（所有類型）與 **所有 UDP 埠號**（port 0-65535），來源為 QTX VPC CIDR。
-   **PG 不是 Cluster**：Spread/Partition 不支援跨 VPC 共享低延遲放置；請使用 **Cluster**。
-   **AZ 字母錯位**：`ap-northeast-1a/1c/1d` 不可靠；請用 **AZ ID（`apne1-az4`）** 對齊。
-   **容量不足**：Cluster PG 經常受限於機架容量，可考慮 **同時發起一批**、**一致的機型**、或使用 **Capacity Reservation in CPG**。

---

## 7. 進階（選配）

-   **Capacity Reservation in CPG（ODCR-C / CR in CPG）**：若對方在 CPG 內建立 **On-Demand Capacity Reservation**，可再透過 RAM 分享；你在開機時指定該 Reservation，以提高在 CPG 內成功啟動的機率。
-   **變更既有實例的放置**：在 **EC2 → Instances → Actions → Instance settings → Modify instance placement** 嘗試加入/更換 PG（停機/再啟動與容量限制可能影響）。
-   **VPC Sharing**（分享子網）：若未採用「各自 VPC + Peering + Shared CPG」，也可讓對方直接分享 **子網**，讓你把實例開在「對方 VPC」內的共享子網與他們的 CPG 中。

---

## 附錄：Console 導覽快速索引

-   **VPC**：`VPC → Your VPCs / Subnets / Route tables / Peering connections`
-   **Peering DNS 設定**：`VPC → Peering connections → Actions → Edit DNS settings`
-   **EC2 Placement Groups**：`EC2 → Network & Security → Placement groups`
-   **AWS RAM**：`AWS RAM → Shared with me / Resource shares`
-   **查看 AZ ID**：`VPC → Subnets（子網詳情）` 或 `EC2 Dashboard（右側 Your AZ IDs）`
