#include <gtest/gtest.h>

#include <numeraire/database/dtos.hpp>
#include <numeraire/enums/exercise_style.hpp>
#include <numeraire/enums/option_type.hpp>
#include <numeraire/products/equity_asset_or_nothing_product.hpp>
#include <numeraire/products/product_factory.hpp>
#include <numeraire/products/vanilla_equity_option_product.hpp>
#include <numeraire/utils/exception.hpp>

TEST(ProductFactoryTest, BuildsVanillaEuropeanFromCatalogRows) {
    numeraire::database::ProductDto product{};
    product.product_id = "P_AAPL_001";
    product.option_side = "CALL";
    product.strike = 233.0;
    product.attributes_json = "{}";

    numeraire::database::ProductEquityDto equity{};
    equity.product_id = "P_AAPL_001";
    equity.asset_kind = "EQUITY";
    equity.underlying_id = "AAPL";
    equity.expiry_date = std::string{"2025-11-04"};

    numeraire::database::TradeHeaderDto header{};
    header.trade_id = "TRD_001";
    header.trade_date = "2025-08-06";

    const auto instrument =
            numeraire::products::ProductFactory::MakeFromEquityCatalog(product, equity, &header);

    ASSERT_NE(instrument, nullptr);
    EXPECT_EQ(instrument->UnderlyingId(), "AAPL");
    EXPECT_EQ(instrument->OptionKind(), numeraire::OptionType::kCall);
    EXPECT_EQ(instrument->Exercise(), numeraire::ExerciseStyle::kEuropean);
    EXPECT_DOUBLE_EQ(instrument->Strike(), 233.0);
    EXPECT_EQ(instrument->TradeDate().year, 2025);
    EXPECT_EQ(instrument->TradeDate().month, 8);
    EXPECT_EQ(instrument->TradeDate().day, 6);
    EXPECT_EQ(instrument->ExpiryDate().year, 2025);
    EXPECT_EQ(instrument->ExpiryDate().month, 11);
    EXPECT_EQ(instrument->ExpiryDate().day, 4);
    EXPECT_EQ(instrument->PaymentSchedule(), nullptr);
}

TEST(ProductFactoryTest, NullTradeUsesExpiryAsTradeDate) {
    numeraire::database::ProductDto product{};
    product.product_id = "P_X_001";
    product.option_side = "PUT";
    product.strike = 100.0;
    product.attributes_json = "";

    numeraire::database::ProductEquityDto equity{};
    equity.product_id = "P_X_001";
    equity.asset_kind = "equity";
    equity.underlying_id = "XOM";
    equity.expiry_date = std::string{"2026-02-06"};

    const auto instrument =
            numeraire::products::ProductFactory::MakeFromEquityCatalog(product, equity, nullptr);

    ASSERT_NE(instrument, nullptr);
    EXPECT_EQ(instrument->TradeDate().year, instrument->ExpiryDate().year);
    EXPECT_EQ(instrument->TradeDate().month, instrument->ExpiryDate().month);
    EXPECT_EQ(instrument->TradeDate().day, instrument->ExpiryDate().day);
}

TEST(ProductFactoryTest, MismatchedProductIdThrows) {
    numeraire::database::ProductDto product{};
    product.product_id = "P_A";
    product.option_side = "CALL";
    product.strike = 1.0;
    product.attributes_json = "{}";

    numeraire::database::ProductEquityDto equity{};
    equity.product_id = "P_B";
    equity.asset_kind = "EQUITY";
    equity.underlying_id = "IBM";
    equity.expiry_date = std::string{"2025-01-02"};

    EXPECT_THROW(static_cast<void>(
                         numeraire::products::ProductFactory::MakeFromEquityCatalog(product, equity, nullptr)),
                 numeraire::ValidationError);
}

TEST(ProductFactoryTest, NonEquityAssetKindThrows) {
    numeraire::database::ProductDto product{};
    product.product_id = "P_1";
    product.option_side = "CALL";
    product.strike = 1.0;
    product.attributes_json = "{}";

    numeraire::database::ProductEquityDto equity{};
    equity.product_id = "P_1";
    equity.asset_kind = "FX";
    equity.underlying_id = "EURUSD";
    equity.expiry_date = std::string{"2025-01-02"};

    EXPECT_THROW(static_cast<void>(
                         numeraire::products::ProductFactory::MakeFromEquityCatalog(product, equity, nullptr)),
                 numeraire::ValidationError);
}

TEST(ProductFactoryTest, AssetOrNothingCatalogBuildsDedicatedProductType) {
    numeraire::database::ProductDto product{};
    product.product_id = "P_BABA_008";
    product.option_side = "PUT";
    product.strike = 130.0;
    product.attributes_json = R"({"instrument_type": "AssetOrNothingOption"})";

    numeraire::database::ProductEquityDto equity{};
    equity.product_id = "P_BABA_008";
    equity.asset_kind = "EQUITY";
    equity.underlying_id = "BABA";
    equity.expiry_date = std::string{"2025-11-07"};

    const auto instrument =
            numeraire::products::ProductFactory::MakeFromEquityCatalog(product, equity, nullptr);

    ASSERT_NE(instrument, nullptr);
    EXPECT_NE(dynamic_cast<numeraire::products::EquityAssetOrNothingProduct*>(instrument.get()), nullptr);
    EXPECT_EQ(dynamic_cast<numeraire::products::VanillaEquityOptionProduct*>(instrument.get()), nullptr);
    EXPECT_EQ(instrument->OptionKind(), numeraire::OptionType::kPut);
    EXPECT_DOUBLE_EQ(instrument->Strike(), 130.0);
    EXPECT_EQ(instrument->UnderlyingId(), "BABA");
}

TEST(ProductFactoryTest, AsianOptionAttributesThrowsUntilSupported) {
    numeraire::database::ProductDto product{};
    product.product_id = "P_NVDA_007";
    product.option_side = "CALL";
    product.strike = 135.0;
    product.attributes_json = R"({"instrument_type": "AsianOption"})";

    numeraire::database::ProductEquityDto equity{};
    equity.product_id = "P_NVDA_007";
    equity.asset_kind = "EQUITY";
    equity.underlying_id = "NVDA";
    equity.expiry_date = std::string{"2025-11-07"};

    EXPECT_THROW(static_cast<void>(
                         numeraire::products::ProductFactory::MakeFromEquityCatalog(product, equity, nullptr)),
                 numeraire::ValidationError);
}
