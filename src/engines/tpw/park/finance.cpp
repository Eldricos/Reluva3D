// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The park's accounts, loans, shares, annual report and bankruptcy:
// docs/engines/tpw/games/simcoaster/systems/finance.md.

#include "engines/tpw/park/park.h"

#include <cmath>
#include <cstdlib>
#include <numeric>

namespace reluva::tpw {

namespace {

// The settings offer up to this many loans (LoanInfo[0] to [7]).
constexpr int kMostLoans = 8;
// Below this, the finance director warns of the bank balance (TAG_SYSTEM
// 106), once till it's back above twice it (guessed).
constexpr int kLowCash = 5000;
// Months in the red to the director's warnings (TAG_SYSTEM 36, 37) and to
// bankruptcy: "If you stay in the red for six months you'll go bankrupt".
constexpr int kRedWarning = 3;
constexpr int kRedLastWarning = 5;
constexpr int kRedMonthsToBankruptcy = 6;
// The annual report's share price moves by the year's profit against
// last year's, as a share of what the company's worth, not taken below
// this (guessed).
constexpr int kLeastWorth = 10000;

// TAG_SYSTEM's messages.
constexpr uint32_t kRepayAdvice = 33;
constexpr uint32_t kFirstInRed = 34;
constexpr uint32_t kInRedAgain = 35;
constexpr uint32_t kThreeMonthsInRed = 36;
constexpr uint32_t kFiveMonthsInRed = 37;
constexpr uint32_t kLowBalance = 106;
constexpr uint32_t kGameOverTitle = 165;
constexpr uint32_t kBankruptLetter = 166;
constexpr uint32_t kLoanRepaymentTitle = 226;
constexpr uint32_t kCashProblemTitle = 227;
constexpr uint32_t kUrgentCashTitle = 228;
constexpr uint32_t kAnnualReportTitle = 262;
constexpr uint32_t kAnnualReportText = 263;
// The share price's news, from the best year to the worst: up by a big
// change, by more than halfway to one, by more than a similar year's, not
// at all, then down likewise.
constexpr uint32_t kSharesUpMost = 264;
constexpr uint32_t kSharesUpMuch = 265;
constexpr uint32_t kSharesUp = 266;
constexpr uint32_t kSharesSame = 267;
constexpr uint32_t kSharesDown = 268;
constexpr uint32_t kSharesDownMuch = 269;
constexpr uint32_t kSharesDownMost = 270;
constexpr uint32_t kRose = 271;
constexpr uint32_t kFell = 272;
constexpr uint32_t kNoChange = 273;
constexpr uint32_t kRevenueLine = 274;
constexpr uint32_t kProfitLine = 275;
constexpr uint32_t kLossLine = 276;
constexpr uint32_t kValueLine = 277;
constexpr uint32_t kVisitorsLine = 278;
constexpr uint32_t kSatisfactionLine = 279;

// A loan's monthly repayment: its amount and interest paid off in equal
// months, the APR a twelfth a month (guessed).
int repaymentOf(int amount, float rate, int months) {
    if (months <= 0)
        return amount;
    const double monthly = double(rate) / 1200;
    if (monthly <= 0)
        return int(std::lround(double(amount) / months));
    return int(std::lround(double(amount) * monthly / (1 - std::pow(1 + monthly, -months))));
}

} // namespace

// The loans on offer and the shares, from the general settings, the world's
// over them (Land of Invention's names other lenders).
void Park::loadFinance(const SamFile &general, const SamFile *world) {
    auto number = [&](const std::string &name, double fallback) {
        const double value = general.number(name, fallback);
        return world ? world->number(name, value) : value;
    };
    for (int i = 0; i < kMostLoans; ++i) {
        const std::string at = "LoanInfo[" + std::to_string(i) + "].";
        Loan offer;
        offer.offer = i;
        offer.amount = int(number(at + "LoanAmount", 0));
        if (offer.amount <= 0)
            continue;
        offer.rate = float(number(at + "APRInPercent", 0));
        offer.months = int(number(at + "RepaymentPeriodInMonths", 12));
        offer.lender = int(number(at + "Lendername", i));
        offer.repayment = repaymentOf(offer.amount, offer.rate, offer.months);
        _offers.push_back(offer);
    }
    _shares.issued = int(number("ShareInfo.CompanySharesIssued", _shares.issued));
    _shares.owned = int(number("ShareInfo.InitialSharesOwned", _shares.owned));
    _shares.price = int(number("ShareInfo.InitialSharePrice", _shares.price));
    _similarPercent = int(number("ShareInfo.AnnRptSimilarPercentage", _similarPercent));
    _bigChangePercent = int(number("ShareInfo.AnnRptBigChangePercentage", _bigChangePercent));
}

// Takings put money in, the rest takes it out; both count in the month's
// and year's accounts.
void Park::book(Ledger line, int amount) {
    _cash += size_t(line) < kIncomeLines ? amount : -amount;
    _thisMonth[size_t(line)] += amount;
    _thisYear[size_t(line)] += amount;
}

int Park::income(const Accounts &a) {
    return std::accumulate(a.begin(), a.begin() + kIncomeLines, 0);
}

int Park::expenditure(const Accounts &a) {
    return std::accumulate(a.begin() + kIncomeLines, a.end(), 0);
}

int Park::parkValue() const {
    int value = 0;
    for (size_t i = 0; i < _objects.size(); ++i)
        value += scrapValue(i);
    return value;
}

std::vector<Park::Loan> Park::availableLoans() const {
    std::vector<Loan> out;
    for (const Loan &offer : _offers)
        if (std::none_of(_loans.begin(), _loans.end(), [&](const Loan &l) { return l.offer == offer.offer; }))
            out.push_back(offer);
    return out;
}

int Park::owed() const {
    int total = 0;
    for (const Loan &l : _loans)
        total += l.balance;
    return total;
}

// Each offer once at a time; it's not takings, so not in the accounts.
bool Park::takeLoan(int offer) {
    for (const Loan &o : availableLoans())
        if (o.offer == offer) {
            Loan taken = o;
            taken.monthsLeft = o.months;
            taken.balance = o.amount;
            _loans.push_back(taken);
            _cash += o.amount;
            _repayAdvised = false;
            return true;
        }
    return false;
}

// What's owed, without the interest of months to come (guessed).
bool Park::repayLoan(size_t loan) {
    if (loan >= _loans.size() || _cash < _loans[loan].balance)
        return false;
    book(Ledger::kLoans, _loans[loan].balance);
    _loans.erase(_loans.begin() + long(loan));
    return true;
}

void Park::adjustPrices(PriceKind kind, Adjustment how, int amount) {
    const auto adjusted = [&](int price) {
        return how == Adjustment::kSetTo      ? amount
               : how == Adjustment::kChangeBy ? price + amount
                                              : int(std::lround(float(price) * float(100 + amount) / 100));
    };
    if (kind == PriceKind::kEntry) {
        setAdmissionFee(adjusted(_admissionFee));
        return;
    }
    // The photo kiosks are shops of their own kind here; the bathrooms are
    // the facilities.
    const Attractions::Kind matching = kind == PriceKind::kRide       ? Attractions::Kind::kRide
                                       : kind == PriceKind::kShop     ? Attractions::Kind::kShop
                                       : kind == PriceKind::kSideshow ? Attractions::Kind::kSideshow
                                                                      : Attractions::Kind::kFacility;
    for (size_t q = 0; q < _queues.size(); ++q) {
        const int object = _queues[q].object;
        const Attractions::Attraction *a = object >= 0 ? _attractions.find(_objects[size_t(object)].id) : nullptr;
        if (a && (kind == PriceKind::kPhoto ? a->photo : a->kind == matching && !a->photo))
            setFare(int(q), adjusted(_queues[q].fare));
    }
}

void Park::post(uint32_t title, uint32_t text, std::map<int, std::string> values) {
    _messages.push_back({title, text, std::move(values), {}});
}

namespace {

// The letters' titles, and the opinions a month an attraction needs
// before they're heard (guessed).
constexpr uint32_t kPriceComplaintsTitle = 248;
constexpr uint32_t kPricingOpportunityTitle = 249;
constexpr int kOpinionsWanted = 10;

} // namespace

void Park::notePrice(size_t object, PriceOpinion opinion) {
    if (object >= _objects.size())
        return;
    PlacedObject &o = _objects[object];
    ++o.opinions;
    o.pricey += opinion == PriceOpinion::kTooHigh || opinion == PriceOpinion::kMuchTooHigh;
    o.cheap += opinion == PriceOpinion::kCheap;
}

// At a month's end, "Price Complaints" and "Pricing Opportunity"
// (TAG_SYSTEM 248, 249): of an attraction with kOpinionsWanted users'
// opinions or more, most of them that it's too dear, or too cheap (guessed);
// "Re: {0} ... this ride" for one of a kind, "some of your rides" for
// more (134 to 142, 143 to 151). Bathrooms are never found too cheap.
void Park::priceLetters() {
    enum Group { kRides, kShops, kSideshows, kPhotos, kBathrooms, kGroups };
    // Each group's letters for one: too dear, too cheap (0 none); for several.
    constexpr std::array<std::array<uint32_t, 4>, kGroups> kLetters{{
        {134, 135, 143, 144},
        {136, 137, 145, 146},
        {138, 139, 147, 148},
        {140, 141, 149, 150},
        {142, 0, 151, 0},
    }};
    // Each group's attractions found too dear, and too cheap.
    std::array<std::array<std::vector<size_t>, 2>, kGroups> found{};
    for (size_t i = 0; i < _objects.size(); ++i) {
        PlacedObject &o = _objects[i];
        const int heard = std::exchange(o.opinions, 0);
        const int dear = std::exchange(o.pricey, 0);
        const int cheap = std::exchange(o.cheap, 0);
        const Attractions::Attraction *a = _attractions.find(o.id);
        if (!a || heard < kOpinionsWanted)
            continue;
        const int group = a->photo                                  ? kPhotos
                          : a->relief                               ? kBathrooms
                          : a->kind == Attractions::Kind::kRide     ? kRides
                          : a->kind == Attractions::Kind::kShop     ? kShops
                          : a->kind == Attractions::Kind::kSideshow ? kSideshows
                                                                    : -1;
        if (group < 0)
            continue;
        if (2 * dear > heard)
            found[size_t(group)][0].push_back(i);
        else if (2 * cheap > heard && kLetters[size_t(group)][1])
            found[size_t(group)][1].push_back(i);
    }
    for (size_t g = 0; g < kGroups; ++g)
        for (size_t which = 0; which < 2; ++which) {
            const std::vector<size_t> &these = found[g][which];
            const uint32_t title = which == 0 ? kPriceComplaintsTitle : kPricingOpportunityTitle;
            if (these.size() == 1) {
                const PlacedObject &o = _objects[these[0]];
                const Attractions::Attraction *a = _attractions.find(o.id);
                post(title, kLetters[g][which], {{0, o.name.empty() && a ? a->name : o.name}});
            } else if (these.size() > 1) {
                post(title, kLetters[g][2 + which]);
            }
        }
}

// Each day: the director's warnings as the money runs low or out; on a
// month's first, its close.
void Park::financeDay() {
    const bool inRed = _cash < 0;
    if (inRed && !_wasInRed) {
        post(kCashProblemTitle, _beenInRed ? kInRedAgain : kFirstInRed);
        _beenInRed = true;
    }
    _wasInRed = inRed;
    if (!inRed && _cash < kLowCash && !_lowWarned) {
        post(kCashProblemTitle, kLowBalance);
        _lowWarned = true;
    } else if (_cash >= 2 * kLowCash) {
        _lowWarned = false;
    }
    if (dateOf(_lastDay).day == 1)
        financeMonth();
}

// A month's close: the loans' repayments, the interest added to what's
// owed; the months in the red counted, six closing the park; advice to
// repay when the money's there; the year's report each January.
void Park::financeMonth() {
    for (Loan &l : _loans) {
        const int interest = int(std::lround(double(l.balance) * l.rate / 1200));
        const int paid = l.monthsLeft <= 1 ? l.balance + interest : std::min(l.repayment, l.balance + interest);
        book(Ledger::kLoans, paid);
        l.balance += interest - paid;
        --l.monthsLeft;
    }
    std::erase_if(_loans, [](const Loan &l) { return l.monthsLeft <= 0 || l.balance <= 0; });
    _lastMonth = std::exchange(_thisMonth, {});

    _monthsInRed = _cash < 0 ? _monthsInRed + 1 : 0;
    if (_monthsInRed == kRedWarning)
        post(kUrgentCashTitle, kThreeMonthsInRed);
    else if (_monthsInRed == kRedLastWarning)
        post(kUrgentCashTitle, kFiveMonthsInRed);
    else if (_monthsInRed >= kRedMonthsToBankruptcy && !_bankrupt) {
        _bankrupt = true;
        post(kGameOverTitle, kBankruptLetter);
    }

    if (!_loans.empty() && _cash >= owed() && !_repayAdvised) {
        post(kLoanRepaymentTitle, kRepayAdvice);
        _repayAdvised = true;
    }
    if (dateOf(_lastDay).month == 0)
        annualReport();
}

// The year just ended against the one before: revenue, profit, park
// value, visitors and their satisfaction (their happiness, guessed), each
// with its change; the share price moved by the profit's change.
void Park::annualReport() {
    float happiness = 0;
    int counted = 0;
    for (const Visitor &v : _people)
        if (v.inPark) {
            happiness += v.happiness;
            ++counted;
        }
    const int revenue = income(_thisYear);
    const int profit = revenue - expenditure(_thisYear);
    const std::array<int, 5> now{revenue, profit, parkValue(), _yearVisitors,
                                 counted ? int(std::lround(happiness / float(counted))) : 0};
    Message report{kAnnualReportTitle, kAnnualReportText, {}, {}};
    const std::array<std::pair<uint32_t, int>, 5> lines{{{kRevenueLine, 12},
                                                         {profit < 0 ? kLossLine : kProfitLine, 13},
                                                         {kValueLine, 14},
                                                         {kVisitorsLine, 15},
                                                         {kSatisfactionLine, 16}}};
    for (size_t i = 0; i < lines.size(); ++i) {
        Message::Line line{lines[i].first, {{lines[i].second, std::to_string(std::abs(now[i]))}}, 0};
        if (_reported && _lastReport[i] != 0) {
            const int change = int(std::lround(100.0 * (now[i] - _lastReport[i]) / std::abs(_lastReport[i])));
            line.suffix = change > 0 ? kRose : change < 0 ? kFell : kNoChange;
            line.values[11] = std::to_string(std::abs(change));
        }
        report.lines.push_back(std::move(line));
    }

    const int worth = std::max(kLeastWorth, _cash - owed() + now[2]);
    const int moved =
        std::clamp(int(std::lround(100.0 * (profit - _lastReport[1]) / worth)), -_bigChangePercent, _bigChangePercent);
    const int halfway = (_similarPercent + _bigChangePercent) / 2;
    uint32_t news = kSharesSame;
    if (std::abs(moved) > _similarPercent) {
        _shares.price = std::max(1, int(std::lround(double(_shares.price) * (100 + moved) / 100)));
        news = moved >= _bigChangePercent    ? kSharesUpMost
               : moved >= halfway            ? kSharesUpMuch
               : moved > 0                   ? kSharesUp
               : moved <= -_bigChangePercent ? kSharesDownMost
               : moved <= -halfway           ? kSharesDownMuch
                                             : kSharesDown;
    }
    report.lines.push_back({news, {{10, std::to_string(std::abs(moved))}}, 0});
    _messages.push_back(std::move(report));
    _lastReport = now;
    _reported = true;
    _thisYear = {};
    _yearVisitors = 0;
}

} // namespace reluva::tpw
