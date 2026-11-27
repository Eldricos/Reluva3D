# SimCoaster: money

> **Status:** partial · **Code:** `src/engines/tpw/park/finance.cpp`, `src/engines/tpw/ui/park_windows.cpp` · **Checked against:** the general settings, the string tables · **Confidence:** guessed unless marked

## Overview

The park's money comes in at the gates and from its attractions, and goes
out on building, running costs, wages and research. The Finance menu
(`UITEXT` 841) opens Financial Statistics, Loans and the Share Portfolio
(842 to 844); the Attractions menu's Attraction Finance Controls (836)
sets prices by kind. The finance director writes about the money in the
park's messages (`TAG_SYSTEM.str`): when it runs low or out, when it's
time to repay a loan, and each year's annual report, which moves the
company's share price. Six months in the red and the park is closed
down. The recording opens none of these windows; the settings
(`levels/Standard.sam`, "Money things" and "Shares Game stuff") and the
strings give the rest.

## How it works

### The accounts

Every sum in or out is booked to a line of the accounts, the lines
Financial Statistics shows (`UITEXT` 389 to 398):

| Line | What's booked |
|---|---|
| Gate takings | entry fees |
| Ride, shop, sideshow, facility takings | fares, by the attraction's kind |
| New attractions costs | attractions bought, upgrades, path and queue laid; less what's sold for scrap |
| Attraction running costs | each month's `RunningCost`, and the cost of goods sold |
| Staff wages | each month's |
| Research, loan repayments | counted in the total expenditure, without lines of their own |

The window shows the bank balance, the park's value (its attractions'
scrap values *(guessed)*), this year's income and expenditure by line,
the year rather than the month or all time *(guessed)*, and last month
in summary: its takings, less its expenditure, and the balance (399 to
402). Money is shown "-$ 500" when it's below nothing, as "Cash  -$ {39}"
(967) has it.

### Loans

`LoanInfo[0]` to `[7]` are on offer: an amount, `APRInPercent`,
`RepaymentPeriodInMonths`, and the lender's name in `LOANNAMES.str` by
`Lendername` (a world's own settings name other lenders: Land of
Invention's offers Loans To Your Door, PoshDosh Lending, Cash In A
Dash and Millstone Finance). Each is taken once at a time, after "Are
you sure you want to take out a loan of ${18}" (421). It's repaid in
equal monthly sums: its amount grown by its APR over half its term,
over its months, rounded down, so § 100,000 at 20 percent over 36
months is 100,000 × 1.2^1.5 ÷ 36, § 3,651 a month *(confirmed)*: all
eight loans of the prebuilt park's save have just these
([tpws.md](../formats/tpws.md#the-bank)). Reluva has what's owed start
at all it'll cost and a repayment made each month's first day, the
last month what's left; a loan paid off at once (417), if the money's
there (419), costs all that's still owed *(guessed)*.

The Loans window lists those available (Lender Name, Loan Term
(months), Amount, Interest Rate, Monthly Repayment, Total Payable, 407 to
412) or outstanding (Lender Name, Original Amount, Monthly Repayment,
Months Remaining, Remaining Balance, 407, 413, 411, 414, 415).

### Shares

The company has issued `ShareInfo.CompanySharesIssued` (30,000)
shares, the player owns `InitialSharesOwned` (1,000), at
`InitialSharePrice` (§ 100): a 3 percent stockholding, as the Personnel
File shows in the recording *(confirmed)*. The Share Portfolio shows the
price, the shares owned, their value and the stake (466 to 470), its
title "Portfolio details for {0}, {12}", the player's name and rank
*(guessed)*. Reluva keeps the price in the player's file, so a park's
changes to it last.

Each 1 January the annual report (`TAG_SYSTEM` 262, 263) gives the year
just ended's revenue, profit, park value, number of visitors and average
visitor satisfaction (274 to 279), each with its change from the year
before (271 to 273), and moves the share price, which the director
explains in one of seven messages (264 to 270), from "what an incredible
year" to "dropped like a stone". `AnnRptSimilarPercentage` (3: "Maximum
percentage considered by the financial director as similar") and
`AnnRptBigChangePercentage` (45: "Minimum percentage considered ... as
a major change") divide them. Reluva moves the price by the year's
profit against last year's, as a share of what the company's worth (its
bank balance, less what it owes, and its park's value, not taken below
§ 10,000), up to the big change either way; within the similar
percentage it stays; past halfway to the big change it's the second
message either way, all of it *(guessed)*. Challenges move the price too, won
or failed ([challenges.md](challenges.md)).

### Running out

`TAG_SYSTEM`'s messages, as Reluva shows them:

- the balance below § 5,000 *(guessed)*: "Your bank balance isn't
  looking very healthy!" (106), under "Cash Problem" (227);
- below nothing: "You have run out of money! If you stay in the red for
  six months you'll go bankrupt." (34; afterwards 35);
- three and five months' firsts in the red (36, 37), under "Urgent Cash
  Problem" (228);
- the sixth, and the park is bankrupt: "you've been in the red for six
  months and the banks are forcing us to close down" (166, "Game Over -
  You Lose!", 165). The park ends when it's put away.
- the money's there to pay off the loans: "why not repay some of the
  money you've borrowed?" (33, "Loan Repayment", 226).

A month counts in the red if it starts in the red *(guessed)*.

### Prices by kind

Attraction Finance Controls (179) sets one kind's prices at once, as the
staff's finance controls do the wages: the kind (Ride Entry Price, Shop
Sale Price, Sideshow Game Price, Bathroom Entry Price, Photo Sale Price,
Park Entry Price, 184 to 189), and the adjustment (180: set to an amount,
change by so much, by a percentage, 181 to 183). Photo Sale Price is the
photo kiosks' ([building.md](building.md#its-window-and-menu)); Shop Sale
Price leaves them out.

## In Reluva

`Park` (`src/engines/tpw/park/finance.cpp`) books the money
(`Park::thisYear`, `lastMonth`), keeps the loans (`availableLoans`,
`takeLoan`, `repayLoan`) and shares, writes the director's messages
(`Park::takeMessages`) and goes bankrupt. `FinanceStatsWindow`,
`LoansWindow`, `PortfolioWindow`, `AttractionFinanceWindow` and
`MessageWindow` (`src/engines/tpw/ui/park_windows.cpp`) are its windows,
laid out by guess in the style of those the recording shows; the
messages arrive in the Message Center ([park-windows.md](park-windows.md#the-message-center)). `reluva-tool tpw window-shot <game folder> <world folder>
<out.png> <stats|loans|owing|portfolio|prices|report>` draws them; `report`
runs the park to its first annual report. The unit tests
(`tpw.unit.park`, `tpw.unit.ui`) take, repay and pay off a loan, set
prices, run a park into the red to bankruptcy, and read a year's report.

## Sources and checks

- **Where the facts come from:** the general settings and their
  comments; `UITEXT.str`, `TAG_SYSTEM.str`, `LOANNAMES.str`,
  `CHALLENGE_GENERIC.str`, `MISSION_DESC.str` ("Profit = Bank Balance
  less Outstanding Loans"); the recording's Personnel File at 1:06.
- **How it was checked:** the loans' repayments worked out by hand; the
  windows looked over in `window-shot`'s pictures. None of it has been
  set against the game.

## Open questions

- The windows' looks; which period Financial Statistics shows; what the
  park's value counts.
- How loans are repaid and charged; whether any can be refused.
- How the annual report moves the share price, and what its
  satisfaction is; when the financial year ends.
- When the director warns of a low balance; how a month in the red is
  counted.
- Golden tickets, and how the player's shares change.
