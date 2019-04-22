/**
 *  @file
 *  @copyright defined in eos/LICENSE.txt
 */
#pragma once

#include "../dappservices/log.hpp"
#include "../dappservices/plist.hpp"
#include "../dappservices/plisttree.hpp"
#include "../dappservices/multi_index.hpp"

#define DAPPSERVICES_ACTIONS() \
  XSIGNAL_DAPPSERVICE_ACTION \
  LOG_DAPPSERVICE_ACTIONS \
  IPFS_DAPPSERVICE_ACTIONS

#define DAPPSERVICE_ACTIONS_COMMANDS() \
  IPFS_SVC_COMMANDS()LOG_SVC_COMMANDS() 

#define CONTRACT_NAME() vgrab

namespace eosiosystem {
   class system_contract;
}

using std::string;


                
CONTRACT_START()
   public:

      ACTION create( name issuer, asset maximum_supply);
      ACTION update( name issuer, asset maximum_supply);
      ACTION issue( name to, asset quantity, string memo );
      ACTION open( name owner, const symbol& symbol, name ram_payer );
      ACTION colddrop( name from, name to, asset quantity, string memo );
      ACTION claim( name owner, const symbol& sym);

      ACTION transfer( name from,
                     name to,
                     asset        quantity,
                     string       memo );
      
      inline asset get_supply( symbol_code sym )const;      
      inline asset get_balance( name owner, symbol_code sym )const;

   private:
      TABLE account {
         asset    balance;
         bool     claimed = false;
         uint64_t primary_key()const { return balance.symbol.code().raw(); }
      };
      
      TABLE currency_stats {
         asset          supply;
         asset          max_supply;
         name   issuer;

         uint64_t primary_key()const { return supply.symbol.code().raw(); }
      };
      
      typedef dapp::multi_index<"vaccounts"_n, account> cold_accounts_t;
      typedef eosio::multi_index<"accounts"_n, account> accounts_t;
      typedef eosio::multi_index<"stat"_n, currency_stats> stats;


      void sub_balance( name owner, asset value );
      void add_balance( name owner, asset value, name ram_payer );
      void swap( name owner, asset quantity );
      
      void sub_cold_balance( name owner, asset value );
      void add_cold_balance( name owner, asset value, name ram_payer );

   public:
      struct transfer_args {
         name  from;
         name  to;
         asset         quantity;
         string        memo;
      };
      
CONTRACT_END((create)(update)(issue)(open)(transfer)(claim)(colddrop))

asset vgrab::get_supply( symbol_code sym )const
{
   stats statstable( _self, sym.raw() );
   const auto& st = statstable.get( sym.raw() );
   return st.supply;
}

asset vgrab::get_balance( name owner, symbol_code sym )const
{
   accounts_t accountstable( _self, owner.value );
   const auto& ac = accountstable.get( sym.raw() );
   return ac.balance;
}

