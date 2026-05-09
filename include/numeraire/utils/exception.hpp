#pragma once

#include <stdexcept>
#include <string>
#include <utility>

namespace numeraire {

/// Base type for all Numeraire++ recoverable failures.
class NumeraireException : public std::runtime_error {
public:
    explicit NumeraireException(std::string message) : std::runtime_error(std::move(message)) {}
};

class ConfigError : public NumeraireException {
public:
    explicit ConfigError(std::string message) : NumeraireException(std::move(message)) {}
};

class ValidationError : public NumeraireException {
public:
    explicit ValidationError(std::string message) : NumeraireException(std::move(message)) {}
};

class ScheduleError : public NumeraireException {
public:
    explicit ScheduleError(std::string message) : NumeraireException(std::move(message)) {}
};

class PricingError : public NumeraireException {
public:
    explicit PricingError(std::string message) : NumeraireException(std::move(message)) {}
};

class MarketDataError : public NumeraireException {
public:
    explicit MarketDataError(std::string message) : NumeraireException(std::move(message)) {}
};

class PersistenceError : public NumeraireException {
public:
    explicit PersistenceError(std::string message) : NumeraireException(std::move(message)) {}
};

}  // namespace numeraire
