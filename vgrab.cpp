/**
 *  @file
 *  @copyright defined in eos/LICENSE.txt
 */

#include "vgrab.hpp"

using namespace eosio;

ACTION vgrab::create( name issuer,
                    asset        maximum_supply)
{
    require_auth( _self );

    auto sym = maximum_supply.symbol;
    eosio_assert( sym.is_valid(), "invalid symbol name" );
    eosio_assert( maximum_supply.is_valid(), "invalid supply");
    eosio_assert( maximum_supply.amount > 0, "max-supply must be positive");

    stats statstable( _self, sym.code().raw() );
    auto existing = statstable.find( sym.code().raw() );
    eosio_assert( existing == statstable.end(), "token with symbol already exists" );

    statstable.emplace( _self, [&]( auto& s ) {
       s.supply.symbol = maximum_supply.symbol;
       s.max_supply    = maximum_supply;
       s.issuer        = issuer;
    });
}

ACTION vgrab::update( name issuer,
                    asset        maximum_supply)
{
    require_auth( _self );

    auto sym = maximum_supply.symbol;
    eosio_assert( sym.is_valid(), "invalid symbol name" );
    eosio_assert( maximum_supply.is_valid(), "invalid supply");
    eosio_assert( maximum_supply.amount > 0, "max-supply must be positive");

    stats statstable( _self, sym.code().raw() );
    auto existing = statstable.find( sym.code().raw() );
    eosio_assert( existing != statstable.end(), "token with symbol does not exist, create token before issue" );
    const auto& st = *existing;

    eosio_assert( maximum_supply.symbol == st.supply.symbol, "symbol precision mismatch" );
    eosio_assert( st.supply.amount <= maximum_supply.amount , "max-supply cannot be less than actual supply");

    statstable.modify( st, eosio::same_payer, [&]( auto& s ) {
        s.max_supply    = maximum_supply;
        s.issuer        = issuer;
    });
}


ACTION vgrab::issue( name to, asset quantity, string memo )
{
    auto sym = quantity.symbol;
    eosio_assert( sym.is_valid(), "invalid symbol name" );
    eosio_assert( memo.size() <= 256, "memo has more than 256 bytes" );

    auto sym_name = sym.code().raw();
    stats statstable( _self, sym_name );
    auto existing = statstable.find( sym_name );
    eosio_assert( existing != statstable.end(), "token with symbol does not exist, create token before issue" );
    const auto& st = *existing;

    require_auth( st.issuer );
    eosio_assert( quantity.is_valid(), "invalid quantity" );
    eosio_assert( quantity.amount > 0, "must issue positive quantity" );

    eosio_assert( quantity.symbol == st.supply.symbol, "symbol precision mismatch" );
    eosio_assert( quantity.amount <= st.max_supply.amount - st.supply.amount, "quantity exceeds available supply");

    statstable.modify( st, eosio::same_payer, [&]( auto& s ) {
       s.supply += quantity;
    });

    add_balance( st.issuer, quantity, st.issuer );

    if( to != st.issuer ) {
       SEND_INLINE_ACTION( *this, transfer, {st.issuer,"issuer"_n}, {st.issuer, to, quantity, memo} );
    }
}

ACTION vgrab::open( name owner, const symbol& symbol, name ram_payer )
{
   require_auth( ram_payer );

   auto sym_code_raw = symbol.code().raw();

   stats statstable( _self, sym_code_raw );
   const auto& st = statstable.get( sym_code_raw, "symbol does not exist" );
   eosio_assert( st.supply.symbol == symbol, "symbol precision mismatch" );

   accounts acnts( _self, owner.value );
   auto it = acnts.find( sym_code_raw );
   if( it == acnts.end() ) {
      acnts.emplace( ram_payer, [&]( auto& a ){
        a.balance = asset{0, symbol};
      });
   }
}


ACTION vgrab::transfer( name from,
                      name to,
                      asset        quantity,
                      string       memo )
{
    eosio_assert( from != to, "cannot transfer to self" );
    require_auth( from );
    eosio_assert( is_account( to ), "to account does not exist");
    auto sym = quantity.symbol.code().raw();
    stats statstable( _self, sym );
    const auto& st = statstable.get( sym );

    require_recipient( from );
    require_recipient( to );

    eosio_assert( quantity.is_valid(), "invalid quantity" );
    eosio_assert( quantity.amount > 0, "must transfer positive quantity" );
    eosio_assert( quantity.symbol == st.supply.symbol, "symbol precision mismatch" );
    eosio_assert( memo.size() <= 256, "memo has more than 256 bytes" );

    sub_balance( from, quantity );
    add_balance( to, quantity, from );

    //we are the destination, do a conversion
    if( to == _self) {
      swap( from, quantity );
    }
}


ACTION vgrab::colddrop( name from,
                        name to,
                        asset        quantity,
                        string       memo ){
    
    auto sym = quantity.symbol.code().raw();
    stats statstable( _self, sym );
    const auto& st = statstable.get( sym );
    require_auth( st.issuer );

    require_recipient( from );
    require_recipient( to );

    eosio_assert( quantity.is_valid(), "invalid quantity" );
    eosio_assert( quantity.amount > 0, "must store positive quantity" );
    eosio_assert( quantity.symbol == st.supply.symbol, "symbol precision mismatch" );
    
    sub_balance( from, quantity);
    add_cold_balance( to, quantity, from);
}


void vgrab::swap( name from, asset quantity ) {
  //confirm quantity
  symbol burn_symbol = symbol("ZKSPLAY",4);
  symbol issue_symbol = symbol("ZKS",0);
  uint64_t issued = 0;
  uint64_t burned = 0;
  std::string memo = "Converted ";
  if(quantity.symbol == burn_symbol) {
    issued = quantity.amount / 1'0000;
    burned = issued * 1'0000;
    uint64_t surplus = quantity.amount - burned;
    //return the surplus ZKSPLAY
    if(surplus > 0)
      SEND_INLINE_ACTION( *this, transfer, 
      {_self,"issuer"_n}, 
      {_self, from, asset(surplus,quantity.symbol), "Surplus ZKSPLAY from swapping to ZKS"} );
    memo += std::string("ZKSPLAY to ZKS");
  } else {
    burn_symbol = symbol("ZKS",0);
    issue_symbol = symbol("ZKSPLAY",4);
    issued = quantity.amount * 1'0000;
    memo += std::string("ZKS to ZKSPLAY");
  }
  eosio_assert(issued > 0, "Swap must be greater than 0");

  auto burned_asset = asset(burned,burn_symbol);
  sub_balance( _self, burned_asset );

  auto issued_asset = asset(issued,issue_symbol);
  add_balance( _self, issued_asset, _self );

  SEND_INLINE_ACTION( *this, transfer, 
  {_self,"issuer"_n}, 
  {_self, from, issued_asset, memo} );
}

ACTION vgrab::claim( name owner, const symbol& sym){ 
    require_auth( owner );
    auto sym_raw = sym.code().raw();
    stats statstable( _self, sym_raw );

    require_recipient( owner );

    const auto& st = statstable.get( sym_raw, "symbol does not exist" );
    eosio_assert( st.supply.symbol == sym, "symbol precision mismatch" );
    
    cold_accounts_t from_acnts( _self, owner.value );
    const auto& from = from_acnts.get( sym_raw, "no balance object found" );

    sub_cold_balance( owner, from.balance);
    add_balance( owner, from.balance, owner);  
}

void vgrab::add_balance( name owner, asset value, name ram_payer)
{
   accounts_t to_acnts( _self, owner.value );
   auto to = to_acnts.find( value.symbol.code().raw() );
   if( to == to_acnts.end() ) {
      to_acnts.emplace(ram_payer, [&]( auto& a ){
        a.balance = value;
        a.claimed = true;
      });
   } else {
      to_acnts.modify( *to, ram_payer, [&]( auto& a ) {
        a.balance += value;
      });
   }
}

void vgrab::sub_balance( name owner, asset value)
{
   accounts_t from_acnts( _self, owner.value );
  const auto& from = from_acnts.get( value.symbol.code().raw(), "no balance object found" );
   eosio_assert( from.balance.amount >= value.amount, "overdrawn balance" );
  if( from.balance.amount == value.amount ) {
      from_acnts.erase( from );
  } else {
      from_acnts.modify( from, eosio::same_payer, [&]( auto& a ) {
          a.balance -= value;
      });
  }
}



void vgrab::add_cold_balance( name owner, asset value, name ram_payer)
{
   cold_accounts_t to_acnts( _self, owner.value );
   auto to = to_acnts.find( value.symbol.code().raw() );
   if( to == to_acnts.end() ) {
      to_acnts.emplace(ram_payer, [&]( auto& a ){
        a.balance = value;
      });
   } else {
      to_acnts.modify( *to, ram_payer, [&]( auto& a ) {
        a.balance += value;
      });
   }
}

void vgrab::sub_cold_balance( name owner, asset value)
{
   cold_accounts_t from_acnts( _self, owner.value );
  const auto& from = from_acnts.get( value.symbol.code().raw(), "no balance object found" );
   eosio_assert( from.balance.amount >= value.amount, "overdrawn balance" );
  if( from.balance.amount == value.amount ) {
      from_acnts.erase( from );
  } else {
      from_acnts.modify( from, eosio::same_payer, [&]( auto& a ) {
          a.balance -= value;
      });
  }
}
