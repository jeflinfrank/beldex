#ifndef BELDEX_NAME_SYSTEM_H
#define BELDEX_NAME_SYSTEM_H

#include "crypto/crypto.h"
#include "cryptonote_config.h"
#include "epee/span.h"
#include "cryptonote_basic/tx_extra.h"
#include "common/fs.h"
#include <oxenc/hex.h>

#include <cassert>
#include <string>

struct sqlite3;
struct sqlite3_stmt;
namespace cryptonote
{
struct checkpoint_t;
struct block;
struct address_parse_info;
class transaction;
struct account_address;
struct tx_extra_beldex_name_system;
class Blockchain;
}; // namespace cryptonote

namespace bns
{

constexpr size_t WALLET_NAME_MAX                  = 64;
constexpr size_t WALLET_ACCOUNT_BINARY_LENGTH_INC_PAYMENT_ID     = 73;  // Wallet will encrypt an identifier (1 byte) a public spend and view key (2x 32 bytes) = 65 bytes plus an additional item for payment id (8 bytes) if necessary. The identifier 0 -> No Subaddress or Payment ID, 1 -> Has Subaddress, 2-> Has Payment ID
constexpr size_t WALLET_ACCOUNT_BINARY_LENGTH_NO_PAYMENT_ID     = 65;
constexpr size_t DOMAIN_NAME_MAX          = 63 + 4; // DNS components name must be at most 63 (+ 4 for .bdx); this limit applies if there is at least one hyphen (and thus includes punycode)
constexpr size_t DOMAIN_NAME_MAX_NOHYPHEN = 32 + 4; // If the name does not contain a - then we restrict it to 32 characters so that it cannot be (and is obviously not) an encoded .bdx address (52 characters)
constexpr size_t BELNET_ADDRESS_BINARY_LENGTH    = sizeof(crypto::ed25519_public_key);
constexpr size_t BCHAT_DISPLAY_NAME_MAX         = 64;
constexpr size_t BCHAT_PUBLIC_KEY_BINARY_LENGTH = 1 + sizeof(crypto::ed25519_public_key); // Bchat keys at prefixed with 0xbd + ed25519 key
constexpr size_t ETH_ADDR_BINARY_LENGTH = 20; // Ethereum address bytes

constexpr size_t NAME_HASH_SIZE = sizeof(crypto::hash);
constexpr size_t NAME_HASH_SIZE_B64_MIN = (4*NAME_HASH_SIZE + 2) / 3; // No padding
constexpr size_t NAME_HASH_SIZE_B64_MAX = (NAME_HASH_SIZE + 2) / 3 * 4; // With padding

constexpr size_t SODIUM_ENCRYPTION_EXTRA_BYTES = 40; // crypto_aead_xchacha20poly1305_ietf_ABYTES (16) + crypto_aead_xchacha20poly1305_ietf_NPUBBYTES (24), but we don't include sodium here

constexpr char BNS_WALLET_TYPE_PRIMARY = 0x00;
constexpr char BNS_WALLET_TYPE_SUBADDRESS = 0x01;
constexpr char BNS_WALLET_TYPE_INTEGRATED = 0x02;

struct mapping_value
{
  static size_t constexpr BUFFER_SIZE = std::max({WALLET_ACCOUNT_BINARY_LENGTH_INC_PAYMENT_ID, BELNET_ADDRESS_BINARY_LENGTH, BCHAT_PUBLIC_KEY_BINARY_LENGTH, ETH_ADDR_BINARY_LENGTH}) + SODIUM_ENCRYPTION_EXTRA_BYTES;

  std::array<uint8_t, BUFFER_SIZE> buffer;
  bool encrypted;
  size_t len;

  std::string      to_string() const { return std::string{to_view()}; }
  std::string_view to_view()   const { return {reinterpret_cast<const char*>(buffer.data()), len}; }
  std::string      to_readable_value(cryptonote::network_type nettype, mapping_type type) const;
  // View the buffer as a encrypted value & nonce pair (the nonce is the last 24 bytes).  For older
  // bchat values the nonce will be all 0 bytes *if* the encrypted value is not the proper length
  // for an including-the-nonce value.  For newer bchat and all others the nonce is always
  // present.
  std::pair<std::basic_string_view<unsigned char>, std::basic_string_view<unsigned char>> value_nonce(mapping_type type) const;
  bool operator==(mapping_value const &other) const { return encrypted == other.encrypted && other.to_view() == to_view(); }
  bool operator==(std::string_view other)     const { return other == to_view(); }

  // Encrypts the mapping value in-place given the name, suitable for storing into the BNS DB.  Only
  // basic overflow validation is attempted, values should be pre-validated in the validate*
  // functions.
  //
  // name - the lower-case ascii string of the record
  // name_hash - pointer to a pre-computed name hash, if available.  If nullptr then the hash is
  //     computed as needed.
  // deprecated_heavy - if true use the deprecated argon2 hashing for the encryption key; this
  //     argument is required for hf15, but shouldn't be used afterwards (except for testing purposes).
  //
  // Return true if encryption was successful, after which *this will now contain the encrypted value.
  //
  // If the value is *already* encrypted this fails via assert (in debug compilation) or returns
  // false.
  //
  // Note that, because encryption uses a random nonce, encrypting the same plaintext value multiple
  // times will result in different encrypted strings.
  bool encrypt(std::string_view name, const crypto::hash* name_hash = nullptr, bool deprecated_heavy = false);

  // Decrypts the mapping value given the name and mapping type.  If the name hash is pre-computed
  // it can be passed in.  As with encrypt(), name must be already lower-case.
  //
  // Returns true if decryption was successful, after which *this will now contain the decrypted value.
  //
  // If the value is *already* decrypted this fails via assert (in debug compilation) or returns
  // false.
  bool decrypt(std::string_view name, mapping_type type, const crypto::hash* name_hash = nullptr);

  // Makes a copy of *this, calls encrypt() on it, and returns it.  Unlike encrypt(), this call
  // leaves `*this` unencrypted and instead returns an encrypted copy.
  mapping_value make_encrypted(std::string_view name, const crypto::hash* name_hash = nullptr, bool deprecated_heavy = false) const;

  // Makes a copy of *this, calls decrypt() on it, and returns it.  Unlike decrypt(), this call
  // leaves `*this` encrypted and instead returns an decrypted copy.
  mapping_value make_decrypted(std::string_view name, const crypto::hash* name_hash = nullptr) const;

  std::optional<cryptonote::address_parse_info> get_wallet_address_info() const;

  // Validate a human readable mapping value representation in 'value' and write the binary form into 'blob'.
  // value: if type is bchat, 66 character hex string of an ed25519 public key (with 05 prefix)
  //                   BELnet, 52 character base32z string of an ed25519 public key
  //                   wallet,  the wallet public address string
  // blob: (optional) if function returns true, validate will load the binary data into blob (ready for encryption via encrypt())
  static bool validate(cryptonote::network_type nettype, mapping_type type, std::string_view value, mapping_value *blob = nullptr, std::string *reason = nullptr);
  // blob: (optional) if function returns true then the value will be loaded into the given
  // mapping_value, ready for decryption via decrypt().
  static bool validate_encrypted(mapping_type type, std::string_view value, mapping_value *blob = nullptr, std::string *reason = nullptr);

  mapping_value();
  mapping_value(std::string encrypted_value, std::string nonce);
};
inline std::ostream &operator<<(std::ostream &os, mapping_value const &v) { return os << oxenc::to_hex(v.to_view()); }

inline std::string_view mapping_type_str(mapping_type type)
{
  switch(type)
  {
    case mapping_type::belnet:        return "belnet"sv; // general type stored in the database; 1 year when in a purchase tx
    case mapping_type::bchat:         return "bchat"sv;
    case mapping_type::wallet:        return "wallet"sv;
    case mapping_type::eth_addr:      return "eth_addr"sv;
    default: assert(false);           return "xx_unhandled_type"sv;
  }
}
inline std::ostream &operator<<(std::ostream &os, mapping_type type) { return os << mapping_type_str(type); }

constexpr bool mapping_type_allowed(uint8_t hf_version, mapping_type type) {
  return (type == mapping_type::bchat && hf_version >= cryptonote::network_version_16)
      || (type == mapping_type::belnet && hf_version >= cryptonote::network_version_17_POS) || (type == mapping_type::wallet && hf_version >= cryptonote::network_version_17_POS);
}

// Returns all mapping types supported for lookup as of the given hardfork.
std::vector<mapping_type> all_mapping_types(uint8_t hf_version);

sqlite3 *init_beldex_name_system(const fs::path& file_path, bool read_only);

/// Returns the integer value used in the database and in RPC lookup calls for the given mapping
/// type.  In particularly this maps all mapping_type::belnet_Xyears values to the underlying value
/// of mapping_type::belnet.
constexpr uint16_t db_mapping_type(bns::mapping_type type) {
  return static_cast<uint16_t>(type);
}
constexpr std::string_view db_mapping_value(bns::mapping_type type) {
  switch(type)
  {
    case mapping_type::bchat: return "encrypted_bchat_value"sv;
    case mapping_type::wallet: return "encrypted_wallet_value"sv;
    case mapping_type::belnet: return "encrypted_belnet_value"sv;
    case mapping_type::eth_addr: return "encrypted_eth_addr_value"sv;
    default: assert(false);             return "xx_unhandled_type"sv;
  }
}
// Returns the length of the given mapping type, in blocks, or std::nullopt if the mapping type never expires.
std::optional<uint64_t> expiry_blocks(cryptonote::network_type nettype, mapping_years map_years);

// Returns *the* proper representation of a name_hash for querying the database, which is 44 base64
// characters (43 significant chars + a padding '=').  External input values should always get
// converted to bytes and then back to base64 through this function (even if initially provided in
// base64) to ensure the correct exact representation.  Input must be exactly 32 bytes (a
// std::runtime_error is raised if this is not the case).
std::string name_hash_bytes_to_base64(std::string_view bytes);

// Similar to the above, but takes a value as any of:
// - 32 bytes
// - 64 hex characters
// - 43 or 44 base64 characters (decoded value must be exactly 32 bytes)
// Returns a string of the canonical base64-encoded value *if* the input was valid, std::nullopt
// otherwise.
std::optional<std::string> name_hash_input_to_base64(std::string_view input);

bool validate_bns_name(std::string name, std::string *reason = nullptr);

std::optional<cryptonote::address_parse_info> encrypted_wallet_value_to_info(std::string name, std::string encrypted_value, std::string nonce);

generic_signature  make_ed25519_signature(crypto::hash const &hash, crypto::ed25519_secret_key const &skey);
generic_owner      make_monero_owner(cryptonote::account_public_address const &owner, bool is_subaddress);
generic_owner      make_ed25519_owner(crypto::ed25519_public_key const &pkey);
bool               parse_owner_to_generic_owner(cryptonote::network_type nettype, std::string_view owner, generic_owner &key, std::string *reason);
std::string        tx_extra_signature(std::string_view value_bchat, std::string_view value_wallet, std::string_view value_belnet, std::string_view value_eth_addr, generic_owner const *owner, generic_owner const *backup_owner, crypto::hash const &prev_txid);

enum struct bns_tx_type { lookup, buy, update, renew };
// Converts a human readable case-insensitive string denoting the mapping type into a value suitable for storing into the BNS DB.
// Currently accepts "bchat" or "belnet" for lookups, buys, updates, and renewals; for buys and renewals also accepts "belnet_Ny[ear]" for N=2,5,10
// Lookups are implied by none of buy/update/renew.
// mapping_type: (optional) if function returns true, the uint16_t value of the 'type' will be set
bool         validate_mapping_type(std::string_view type, uint8_t hf_version, mapping_type *mapping_type, std::string *reason);

// Hashes an BNS name.  The name must already be lower-case (but this is only checked in debug builds).
crypto::hash name_to_hash(std::string_view name, const std::optional<crypto::hash>& key = std::nullopt); // Takes a human readable name and hashes it.  Takes an optional value to use as a key to produce a keyed hash.
std::string  name_to_base64_hash(std::string_view name); // Takes a human readable name, hashes it and returns a base64 representation of the hash, suitable for storage into the BNS DB.

struct owner_record
{
  operator bool() const { return loaded; }
  bool loaded;

  int64_t id;
  generic_owner address;
};

struct settings_record
{
  operator bool() const { return loaded; }
  bool loaded;

  uint64_t     top_height;
  crypto::hash top_hash;
  int          version;
};

struct mapping_record
{
  // NOTE: We keep expired entries in the DB indefinitely because we need to
  // keep all BNS entries indefinitely to support large blockchain detachments.
  // A mapping_record forms a linked list of TXID's which allows us to revert
  // the BNS DB to any arbitrary height at a small additional storage cost.
  // return: if the record exists and hasn't expired.
  bool active(uint64_t blockchain_height) const;
  operator bool() const { return loaded; }

  bool          loaded;
  int64_t       id;
  std::string   name_hash; // name hashed and represented in base64 encoding
  mapping_value encrypted_bchat_value;
  mapping_value encrypted_wallet_value;
  mapping_value encrypted_belnet_value;
  mapping_value encrypted_eth_addr_value;
  uint64_t      register_height;
  std::optional<uint64_t> expiration_height;
  uint64_t      update_height;
  crypto::hash  txid;
  crypto::hash  prev_txid;
  int64_t       owner_id;
  int64_t       backup_owner_id;
  generic_owner owner;
  generic_owner backup_owner;
};

struct name_system_db;
class sql_compiled_statement final
{
public:
  /// The name_system_db upon which this object operates
  name_system_db& nsdb;
  /// The stored, owned statement
  sqlite3_stmt* statement = nullptr;

  /// Constructor; takes a reference to the name_system_db.
  explicit sql_compiled_statement(name_system_db& nsdb) : nsdb{nsdb} {}

  /// Non-copyable (because we own an internal sqlite3 statement handle)
  sql_compiled_statement(const sql_compiled_statement&) = delete;
  sql_compiled_statement& operator=(const sql_compiled_statement&) = delete;

  /// Move construction; ownership of the internal statement handle, if present, is transferred to
  /// the new object.
  sql_compiled_statement(sql_compiled_statement&& from) : nsdb{from.nsdb}, statement{from.statement} { from.statement = nullptr; }

  /// Move copying.  The referenced name_system_db must be the same.  Ownership of the internal
  /// statement handle is transferred.  If the target already has a statement handle then it is
  /// destroyed.
  sql_compiled_statement& operator=(sql_compiled_statement&& from);

  /// Destroys the internal sqlite3 statement on destruction
  ~sql_compiled_statement();

  /// Attempts to prepare the given statement.  MERRORs and returns false on failure.  If the object
  /// already has a prepare statement then it is finalized first.
  bool compile(std::string_view query, bool optimise_for_multiple_usage = true);

  /// Returns true if the object owns a prepared statement
  explicit operator bool() const { return statement != nullptr; }

};

struct name_system_db
{
  bool                        init        (cryptonote::Blockchain const *blockchain, cryptonote::network_type nettype, sqlite3 *db);
  bool                        add_block   (const cryptonote::block& block, const std::vector<cryptonote::transaction>& txs);

  cryptonote::network_type    network_type() const { return nettype; }
  uint64_t                    height      () const { return last_processed_height; }

  // Signifies the blockchain has reorganized commences the rollback and pruning procedures.
  void                        block_detach   (cryptonote::Blockchain const &blockchain, uint64_t new_blockchain_height);
  bool                        save_owner     (generic_owner const &owner, int64_t *row_id);
  bool                        save_mapping   (crypto::hash const &tx_hash, cryptonote::tx_extra_beldex_name_system const &src, uint64_t height, std::optional<uint64_t> expiration_height, int64_t owner_id, std::optional<int64_t> backup_owner_id);
  bool                        save_settings  (uint64_t top_height, crypto::hash const &top_hash, int version);

  // Delete all mappings that are registered on height or newer followed by deleting all owners no longer referenced in the DB
  bool                        prune_db(uint64_t height);

  owner_record                get_owner_by_key      (generic_owner const &owner);
  owner_record                get_owner_by_id       (int64_t owner_id);
  // Returns a wallet address from the passed BNS name in "str"
  bool  get_wallet_mapping    (std::string str, uint64_t blockchain_height, cryptonote::address_parse_info& addr_info);
  // The get_mapping* methods can return any mapping, or only active mappings: for only active
  // mappings, pass in the blockchain height.  If you omit it (or explicitly pass std::nullopt) then
  // you will get the latest mappingsvalues regardless of whether expired or not they are expired.
  mapping_record              get_mapping           (std::string_view name_base64_hash, std::optional<uint64_t> blockchain_height = std::nullopt);
  std::vector<mapping_record> get_mappings          (std::string_view name_base64_hash, std::optional<uint64_t> blockchain_height = std::nullopt);
  std::vector<mapping_record> get_mappings_by_owner (generic_owner const &key, std::optional<uint64_t> blockchain_height = std::nullopt);
  std::vector<mapping_record> get_mappings_by_owners(std::vector<generic_owner> const &keys, std::optional<uint64_t> blockchain_height = std::nullopt);
  settings_record             get_settings          ();

  // Returns the count of each type of BNS registration that is currently active.
  int get_mapping_counts(uint64_t blockchain_height);

  // Resolves a mapping of the given type and name hash. Returns a null optional if the value was
  // not found or expired, otherwise returns the encrypted value.
  std::optional<mapping_value> resolve(mapping_type type, std::string_view name_hash_b64, uint64_t blockchain_height);

  // Validates an BNS transaction.  If the function returns true then entry will be populated with
  // the BNS details.  On a false return, `reason` is instead populated with the failure reason.
  bool validate_bns_tx(uint8_t hf_version, uint64_t blockchain_height, cryptonote::transaction const &tx, cryptonote::tx_extra_beldex_name_system &entry, std::string *reason);

  // Destructor; closes the sqlite3 database if one is open
  ~name_system_db();

  sqlite3 *db               = nullptr;
  bool    transaction_begun = false;
private:
  cryptonote::network_type nettype;
  uint64_t last_processed_height = 0;
  crypto::hash last_processed_hash = crypto::null_hash;
  sql_compiled_statement save_owner_sql{*this};
  sql_compiled_statement save_mapping_sql{*this};
  sql_compiled_statement save_settings_sql{*this};
  sql_compiled_statement get_owner_by_key_sql{*this};
  sql_compiled_statement get_owner_by_id_sql{*this};
  sql_compiled_statement get_mapping_sql{*this};
  sql_compiled_statement resolve_sql{*this};
  sql_compiled_statement get_settings_sql{*this};
  sql_compiled_statement prune_mappings_sql{*this};
  sql_compiled_statement prune_owners_sql{*this};
  sql_compiled_statement get_mappings_by_owner_sql{*this};
  sql_compiled_statement get_mapping_counts_sql{*this};
  sql_compiled_statement get_mappings_on_height_and_newer_sql{*this};
};

}; // namespace master_nodes
#endif // BELDEX_NAME_SYSTEM_H
