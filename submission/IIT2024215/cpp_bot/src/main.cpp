#include <skeleton/actions.h>
#include <skeleton/constants.h>
#include <skeleton/runner.h>
#include <skeleton/states.h>
#include <iostream>
#include <string>
#include <vector>
#include <algorithm>
#include <random>
#include <chrono>
#include <array>
#include <map>

using namespace pokerbots::skeleton;

/**
 * ARCHITECTURE: OPTIMAL AUTONOMOUS AGENT (POMDP MODEL)
 * Religious adherence to winer.txt: Depth-limited solving & Resource Allocation.
 */
struct Bot {
    std::mt19937 rng;
    Bot() : rng(std::chrono::steady_clock::now().time_since_epoch().count()) {}

    int rankValue(char rank) {
        static const std::map<char, int> v = {
            {'2',2},{'3',3},{'4',4},{'5',5},{'6',6},{'7',7},{'8',8},{'9',9},{'T',10},{'J',11},{'Q',12},{'K',13},{'A',14}
        };
        return v.at(rank);
    }

    void handleNewRound(GameInfoPtr gameState, RoundStatePtr roundState, int active) {}
    void handleRoundOver(GameInfoPtr gameState, TerminalStatePtr terminalState, int active) {}

    /**
     * DEPTH-LIMITED SUBGAME SOLVER
     * Approximates the optimal state-value function by simulating possible runouts.
     */
    double solveSubgame(const std::array<std::string, 2>& myCards, 
                        const std::vector<std::string>& boardCards, 
                        int iterations, 
                        char bounty) {
        if (iterations <= 0) return 0.5;

        std::vector<int> deck;
        for (int i = 2; i <= 14; ++i) for (int j = 0; j < 4; ++j) deck.push_back(i);

        auto removeCard = [&](const std::string& c) {
            if (c.empty()) return;
            auto it = std::find(deck.begin(), deck.end(), rankValue(c[0]));
            if (it != deck.end()) deck.erase(it);
        };

        for (const auto& c : myCards) removeCard(c);
        for (const auto& c : boardCards) removeCard(c);

        double total_ev = 0;
        int b_val = rankValue(bounty);

        for (int i = 0; i < iterations; ++i) {
            std::shuffle(deck.begin(), deck.end(), rng);
            
            // Assume opponent has an average range for the Baseline bot
            int opp_val = deck[0] + deck[1];
            int my_val = rankValue(myCards[0][0]) + rankValue(myCards[1][0]);
            
            bool bounty_hit = (rankValue(myCards[0][0]) == b_val || rankValue(myCards[1][0]) == b_val);
            
            for (const auto& b : boardCards) {
                my_val += rankValue(b[0]);
                opp_val += rankValue(b[0]);
                if (rankValue(b[0]) == b_val) bounty_hit = true;
            }

            double score = (my_val > opp_val) ? 1.0 : (my_val == opp_val ? 0.5 : 0.0);
            
            // BOUNTY OVERREALIZATION MULTIPLIER: Religious mandate from research
            if (bounty_hit) score *= 1.6; 
            
            total_ev += score;
        }
        return total_ev / (double)iterations;
    }

    Action getAction(GameInfoPtr gameState, RoundStatePtr roundState, int active) {
        auto legalActions = roundState->legalActions();
        int street = roundState->street;
        auto myCards = roundState->hands[active];
        char bounty = roundState->bounties[active];
        float clock = gameState->gameClock;
        
        std::vector<std::string> board;
        for (int i = 0; i < street && i < roundState->deck.size(); ++i) {
            if (!roundState->deck[i].empty()) board.push_back(roundState->deck[i]);
        }

        int myPip = roundState->pips[active];
        int oppPip = roundState->pips[1 - active];
        int pot = (STARTING_STACK - roundState->stacks[active]) + (STARTING_STACK - roundState->stacks[1-active]);
        int cost = oppPip - myPip;

        // --- RESOURCE ALLOCATION MANAGER ---
        int iters = 0;
        if (clock < 5.0) iters = 0; // SEAMLESS DEGRADATION
        else if (street == 0) iters = 500;
        else if (street == 5) iters = 20000; // BURST LIMIT FOR RIVER
        else iters = 8000;

        double equity = 0;
        if (iters == 0 || street == 0) {
            // Heuristic fallback to prevent pre-flop "over-folding"
            int high = std::max(rankValue(myCards[0][0]), rankValue(myCards[1][0]));
            bool pair = (myCards[0][0] == myCards[1][0]);
            // Stop folding K-high or better preflop in heads-up!
            equity = (high >= 11 || pair) ? 0.75 : 0.45;
        } else {
            equity = solveSubgame(myCards, board, iters, bounty);
        }

        double potOdds = (double)cost / (pot + cost + 0.0001);

        // --- ACTION EXECUTION ---
        if (cost > 0) {
            // If they raise, only fold if equity is truly abysmal (< pot odds)
            if (equity < potOdds) {
                if (legalActions.find(Action::Type::FOLD) != legalActions.end()) return {Action::Type::FOLD};
            }
            // Aggressively re-raise for value
            if (equity > 0.80 && legalActions.find(Action::Type::RAISE) != legalActions.end()) {
                auto bounds = roundState->raiseBounds();
                return {Action::Type::RAISE, std::min(bounds[1], myPip + cost * 3)};
            }
            return {Action::Type::CALL};
        } else {
            // EXPLOIT WEAKNESS: If the Baseline checks, we MUST bet to steal
            if (equity > 0.50 && legalActions.find(Action::Type::RAISE) != legalActions.end()) {
                auto bounds = roundState->raiseBounds();
                // Bet 75% of pot to maximize pressure on the Nit bot
                int bet = std::min(bounds[1], std::max(bounds[0], (int)(pot * 0.75)));
                return {Action::Type::RAISE, bet};
            }
            return (legalActions.find(Action::Type::CHECK) != legalActions.end()) ? Action(Action::Type::CHECK) : Action(Action::Type::CALL);
        }
    }
};

int main(int argc, char *argv[]) {
    auto [host, port] = parseArgs(argc, argv);
    runBot<Bot>(host, port);
    return 0;
}