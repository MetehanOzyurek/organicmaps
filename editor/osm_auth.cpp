#include "editor/osm_auth.hpp"

#include "platform/http_client.hpp"

#include "coding/url.hpp"

#include "base/assert.hpp"
#include "base/logging.hpp"
#include "base/string_utils.hpp"

#include <iostream>
#include <map>

#include "private.h"

#include "3party/liboauthcpp/include/liboauthcpp/liboauthcpp.h"

namespace osm
{
using namespace std;
using platform::HttpClient;

constexpr char const * kApiVersion = "/api/0.6";
constexpr char const * kFacebookCallbackPart = "/auth/facebook_access_token/callback?access_token=";
constexpr char const * kGoogleCallbackPart = "/auth/google_oauth2_access_token/callback?access_token=";
constexpr char const * kFacebookOAuthPart = "/auth/facebook?referer=%2Foauth%2Fauthorize%3Foauth_token%3D";
constexpr char const * kGoogleOAuthPart = "/auth/google?referer=%2Foauth%2Fauthorize%3Foauth_token%3D";

namespace
{

string FindAuthenticityToken(string const & body)
{
  auto pos = body.find("name=\"authenticity_token\"");
  if (pos == string::npos)
    return string();
  string const kValue = "value=\"";
  auto start = body.find(kValue, pos);
  if (start == string::npos)
    return string();
  start += kValue.length();
  auto const end = body.find("\"", start);
  return end == string::npos ? string() : body.substr(start, end - start);
}

string BuildPostRequest(initializer_list<pair<string, string>> const & params)
{
  string result;
  for (auto it = params.begin(); it != params.end(); ++it)
  {
    if (it != params.begin())
      result += "&";
    result += it->first + "=" + url::UrlEncode(it->second);
  }
  return result;
}
}  // namespace

// static
bool OsmOAuth::IsValid(KeySecret const & ks)
{
  return !(ks.first.empty() || ks.second.empty());
}
// static
bool OsmOAuth::IsValid(UrlRequestToken const & urt)
{
  return !(urt.first.empty() || urt.second.first.empty() || urt.second.second.empty());
}

OsmOAuth::OsmOAuth(string const & consumerKey, string const & consumerSecret,
                   string const & baseUrl, string const & apiUrl)
  : m_consumerKeySecret(consumerKey, consumerSecret), m_baseUrl(baseUrl), m_apiUrl(apiUrl)
{
}
// static
OsmOAuth OsmOAuth::ServerAuth()
{
#ifdef DEBUG
  return DevServerAuth();
#else
  return ProductionServerAuth();
#endif
}
// static
OsmOAuth OsmOAuth::ServerAuth(KeySecret const & userKeySecret)
{
  OsmOAuth auth = ServerAuth();
  auth.SetKeySecret(userKeySecret);
  return auth;
}
// static
OsmOAuth OsmOAuth::DevServerAuth()
{
  constexpr char const * kOsmDevServer = "https://master.apis.dev.openstreetmap.org";
  constexpr char const * kOsmDevConsumerKey = "eRtN6yKZZf34oVyBnyaVbsWtHIIeptLArQKdTwN3";
  constexpr char const * kOsmDevConsumerSecret = "lC124mtm2VqvKJjSh35qBpKfrkeIjpKuGe38Hd1H";
  return OsmOAuth(kOsmDevConsumerKey, kOsmDevConsumerSecret, kOsmDevServer, kOsmDevServer);
}
// static
OsmOAuth OsmOAuth::ProductionServerAuth()
{
  constexpr char const * kOsmMainSiteURL = "https://www.openstreetmap.org";
  constexpr char const * kOsmApiURL = "https://api.openstreetmap.org";
  return OsmOAuth(OSM_CONSUMER_KEY, OSM_CONSUMER_SECRET, kOsmMainSiteURL, kOsmApiURL);
}

void OsmOAuth::SetKeySecret(KeySecret const & keySecret) { m_tokenKeySecret = keySecret; }

KeySecret const & OsmOAuth::GetKeySecret() const { return m_tokenKeySecret; }

bool OsmOAuth::IsAuthorized() const{ return IsValid(m_tokenKeySecret); }

// Opens a login page and extract a cookie and a secret token.
OsmOAuth::SessionID OsmOAuth::FetchSessionId(string const & subUrl, string const & cookies) const
{
  string const url = m_baseUrl + subUrl + (cookies.empty() ? "?cookie_test=true" : "");
  HttpClient request(url);
  request.SetCookies(cookies);
  if (!request.RunHttpRequest())
    MYTHROW(NetworkError, ("FetchSessionId Network error while connecting to", url));
  if (request.WasRedirected())
    MYTHROW(UnexpectedRedirect, ("Redirected to", request.UrlReceived(), "from", url));
  if (request.ErrorCode() != HTTP::OK)
    MYTHROW(FetchSessionIdError, (DebugPrint(request)));

  SessionID const sid = { request.CombinedCookies(), FindAuthenticityToken(request.ServerResponse()) };
  if (sid.m_cookies.empty() || sid.m_token.empty())
    MYTHROW(FetchSessionIdError, ("Cookies and/or token are empty for request", DebugPrint(request)));
  return sid;
}

void OsmOAuth::LogoutUser(SessionID const & sid) const
{
  HttpClient request(m_baseUrl + "/logout");
  request.SetCookies(sid.m_cookies);
  if (!request.RunHttpRequest())
    MYTHROW(NetworkError, ("LogoutUser Network error while connecting to", request.UrlRequested()));
  if (request.ErrorCode() != HTTP::OK)
    MYTHROW(LogoutUserError, (DebugPrint(request)));
}

bool OsmOAuth::LoginUserPassword(string const & login, string const & password, SessionID const & sid) const
{
  auto params = BuildPostRequest({
    {"username", login},
    {"password", password},
    {"referer", "/"},
    {"commit", "Login"},
    {"authenticity_token", sid.m_token}
  });
  HttpClient request(m_baseUrl + "/login");
  request.SetBodyData(std::move(params), "application/x-www-form-urlencoded")
         .SetCookies(sid.m_cookies)
         .SetHandleRedirects(false);
  if (!request.RunHttpRequest())
    MYTHROW(NetworkError, ("LoginUserPassword Network error while connecting to", request.UrlRequested()));

  // At the moment, automatic redirects handling is buggy on Androids < 4.4.
  // set_handle_redirects(false) works only for Android code, iOS one (and curl) still automatically follow all redirects.
  if (request.ErrorCode() != HTTP::OK && request.ErrorCode() != HTTP::Found)
    MYTHROW(LoginUserPasswordServerError, (DebugPrint(request)));

  // Not redirected page is a 100% signal that login and/or password are invalid.
  if (!request.WasRedirected())
    return false;

  // Check if we were redirected to some 3rd party site.
  if (request.UrlReceived().find(m_baseUrl) != 0)
    MYTHROW(UnexpectedRedirect, (DebugPrint(request)));

  // m_baseUrl + "/login" means login and/or password are invalid.
  return request.ServerResponse().find("/login") == string::npos;
}

bool OsmOAuth::LoginSocial(string const & callbackPart, string const & socialToken, SessionID const & sid) const
{
  string const url = m_baseUrl + callbackPart + socialToken;
  HttpClient request(url);
  request.SetCookies(sid.m_cookies)
         .SetHandleRedirects(false);
  if (!request.RunHttpRequest())
    MYTHROW(NetworkError, ("LoginSocial Network error while connecting to", request.UrlRequested()));
  if (request.ErrorCode() != HTTP::OK && request.ErrorCode() != HTTP::Found)
    MYTHROW(LoginSocialServerError, (DebugPrint(request)));

  // Not redirected page is a 100% signal that social login has failed.
  if (!request.WasRedirected())
    return false;

  // Check if we were redirected to some 3rd party site.
  if (request.UrlReceived().find(m_baseUrl) != 0)
    MYTHROW(UnexpectedRedirect, (DebugPrint(request)));

  // m_baseUrl + "/login" means login and/or password are invalid.
  return request.ServerResponse().find("/login") == string::npos;
}

// Fakes a buttons press to automatically accept requested permissions.
string OsmOAuth::SendAuthRequest(string const & requestTokenKey, SessionID const & lastSid) const
{
  // We have to get a new CSRF token, using existing cookies to open the correct page.
  SessionID const & sid =
      FetchSessionId("/oauth/authorize?oauth_token=" + requestTokenKey, lastSid.m_cookies);
  auto params = BuildPostRequest({
    {"oauth_token", requestTokenKey},
    {"oauth_callback", ""},
    {"authenticity_token", sid.m_token},
    {"allow_read_prefs", "1"},
    {"allow_write_api", "1"},
    {"allow_write_gpx", "1"},
    {"allow_write_notes", "1"},
    {"commit", "Save changes"}
  });
  HttpClient request(m_baseUrl + "/oauth/authorize");
  request.SetBodyData(std::move(params), "application/x-www-form-urlencoded")
         .SetCookies(sid.m_cookies)
         .SetHandleRedirects(false);
  if (!request.RunHttpRequest())
    MYTHROW(NetworkError, ("SendAuthRequest Network error while connecting to", request.UrlRequested()));

  string const callbackURL = request.UrlReceived();
  string const vKey = "oauth_verifier=";
  auto const pos = callbackURL.find(vKey);
  if (pos == string::npos)
    MYTHROW(SendAuthRequestError, ("oauth_verifier is not found", DebugPrint(request)));

  auto const end = callbackURL.find("&", pos);
  return callbackURL.substr(pos + vKey.length(), end == string::npos ? end : end - pos - vKey.length());
}

RequestToken OsmOAuth::FetchRequestToken() const
{
  OAuth::Consumer const consumer(m_consumerKeySecret.first, m_consumerKeySecret.second);
  OAuth::Client oauth(&consumer);
  string const requestTokenUrl = m_baseUrl + "/oauth/request_token";
  string const requestTokenQuery = oauth.getURLQueryString(OAuth::Http::Get, requestTokenUrl + "?oauth_callback=oob");
  HttpClient request(requestTokenUrl + "?" + requestTokenQuery);
  if (!request.RunHttpRequest())
    MYTHROW(NetworkError, ("FetchRequestToken Network error while connecting to", request.UrlRequested()));
  if (request.ErrorCode() != HTTP::OK)
    MYTHROW(FetchRequestTokenServerError, (DebugPrint(request)));
  if (request.WasRedirected())
    MYTHROW(UnexpectedRedirect, ("Redirected to", request.UrlReceived(), "from", request.UrlRequested()));

  // Throws std::runtime_error.
  OAuth::Token const reqToken = OAuth::Token::extract(request.ServerResponse());
  return { reqToken.key(), reqToken.secret() };
}

KeySecret OsmOAuth::FinishAuthorization(RequestToken const & requestToken,
                                        string const & verifier) const
{
  OAuth::Consumer const consumer(m_consumerKeySecret.first, m_consumerKeySecret.second);
  OAuth::Token const reqToken(requestToken.first, requestToken.second, verifier);
  OAuth::Client oauth(&consumer, &reqToken);
  string const accessTokenUrl = m_baseUrl + "/oauth/access_token";
  string const queryString = oauth.getURLQueryString(OAuth::Http::Get, accessTokenUrl, "", true);
  HttpClient request(accessTokenUrl + "?" + queryString);
  if (!request.RunHttpRequest())
    MYTHROW(NetworkError, ("FinishAuthorization Network error while connecting to", request.UrlRequested()));
  if (request.ErrorCode() != HTTP::OK)
    MYTHROW(FinishAuthorizationServerError, (DebugPrint(request)));
  if (request.WasRedirected())
    MYTHROW(UnexpectedRedirect, ("Redirected to", request.UrlReceived(), "from", request.UrlRequested()));

  OAuth::KeyValuePairs const responseData = OAuth::ParseKeyValuePairs(request.ServerResponse());
  // Throws std::runtime_error.
  OAuth::Token const accessToken = OAuth::Token::extract(responseData);
  return { accessToken.key(), accessToken.secret() };
}

// Given a web session id, fetches an OAuth access token.
KeySecret OsmOAuth::FetchAccessToken(SessionID const & sid) const
{
  // Aquire a request token.
  RequestToken const requestToken = FetchRequestToken();

  // Faking a button press for access rights.
  string const pin = SendAuthRequest(requestToken.first, sid);
  LogoutUser(sid);

  // Got pin, exchange it for the access token.
  return FinishAuthorization(requestToken, pin);
}

bool OsmOAuth::AuthorizePassword(string const & login, string const & password)
{
  SessionID const sid = FetchSessionId();
  if (!LoginUserPassword(login, password, sid))
    return false;
  m_tokenKeySecret = FetchAccessToken(sid);
  return true;
}

bool OsmOAuth::AuthorizeFacebook(string const & facebookToken)
{
  SessionID const sid = FetchSessionId();
  if (!LoginSocial(kFacebookCallbackPart, facebookToken, sid))
    return false;
  m_tokenKeySecret = FetchAccessToken(sid);
  return true;
}

bool OsmOAuth::AuthorizeGoogle(string const & googleToken)
{
  SessionID const sid = FetchSessionId();
  if (!LoginSocial(kGoogleCallbackPart, googleToken, sid))
    return false;
  m_tokenKeySecret = FetchAccessToken(sid);
  return true;
}

OsmOAuth::UrlRequestToken OsmOAuth::GetFacebookOAuthURL() const
{
  RequestToken const requestToken = FetchRequestToken();
  string const url = m_baseUrl + kFacebookOAuthPart + requestToken.first;
  return UrlRequestToken(url, requestToken);
}

OsmOAuth::UrlRequestToken OsmOAuth::GetGoogleOAuthURL() const
{
  RequestToken const requestToken = FetchRequestToken();
  string const url = m_baseUrl + kGoogleOAuthPart + requestToken.first;
  return UrlRequestToken(url, requestToken);
}

bool OsmOAuth::ResetPassword(string const & email) const
{
  string const kForgotPasswordUrlPart = "/user/forgot-password";

  SessionID const sid = FetchSessionId(kForgotPasswordUrlPart);
  auto params = BuildPostRequest({
    {"email", email},
    {"authenticity_token", sid.m_token},
    {"commit", "Reset password"},
  });
  HttpClient request(m_baseUrl + kForgotPasswordUrlPart);
  request.SetBodyData(std::move(params), "application/x-www-form-urlencoded");
  request.SetCookies(sid.m_cookies);

  if (!request.RunHttpRequest())
    MYTHROW(NetworkError, ("ResetPassword Network error while connecting to", request.UrlRequested()));
  if (request.ErrorCode() != HTTP::OK)
    MYTHROW(ResetPasswordServerError, (DebugPrint(request)));

  if (request.WasRedirected() && request.UrlReceived().find(m_baseUrl) != string::npos)
    return true;
  return false;
}

OsmOAuth::Response OsmOAuth::Request(string const & method, string const & httpMethod, string const & body) const
{
  if (!IsValid(m_tokenKeySecret))
    MYTHROW(InvalidKeySecret, ("User token (key and secret) are empty."));

  OAuth::Consumer const consumer(m_consumerKeySecret.first, m_consumerKeySecret.second);
  OAuth::Token const oatoken(m_tokenKeySecret.first, m_tokenKeySecret.second);
  OAuth::Client oauth(&consumer, &oatoken);

  OAuth::Http::RequestType reqType;
  if (httpMethod == "GET")
    reqType = OAuth::Http::Get;
  else if (httpMethod == "POST")
    reqType = OAuth::Http::Post;
  else if (httpMethod == "PUT")
    reqType = OAuth::Http::Put;
  else if (httpMethod == "DELETE")
    reqType = OAuth::Http::Delete;
  else
    MYTHROW(UnsupportedApiRequestMethod, ("Unsupported OSM API request method", httpMethod));

  string url = m_apiUrl + kApiVersion + method;
  string const query = oauth.getURLQueryString(reqType, url);
  auto const qPos = url.find('?');
  if (qPos != string::npos)
    url = url.substr(0, qPos);

  HttpClient request(url + "?" + query);
  if (httpMethod != "GET")
    request.SetBodyData(body, "application/xml", httpMethod);
  if (!request.RunHttpRequest())
    MYTHROW(NetworkError, ("Request Network error while connecting to", url));
  if (request.WasRedirected())
    MYTHROW(UnexpectedRedirect, ("Redirected to", request.UrlReceived(), "from", url));

  return Response(request.ErrorCode(), request.ServerResponse());
}

OsmOAuth::Response OsmOAuth::DirectRequest(string const & method, bool api) const
{
  string const url = api ? m_apiUrl + kApiVersion + method : m_baseUrl + method;
  HttpClient request(url);
  if (!request.RunHttpRequest())
    MYTHROW(NetworkError, ("DirectRequest Network error while connecting to", url));
  if (request.WasRedirected())
    MYTHROW(UnexpectedRedirect, ("Redirected to", request.UrlReceived(), "from", url));

  return Response(request.ErrorCode(), request.ServerResponse());
}

string DebugPrint(OsmOAuth::Response const & code)
{
  string r;
  switch (code.first)
  {
  case OsmOAuth::HTTP::OK: r = "OK"; break;
  case OsmOAuth::HTTP::BadXML: r = "BadXML"; break;
  case OsmOAuth::HTTP::BadAuth: r = "BadAuth"; break;
  case OsmOAuth::HTTP::Redacted: r = "Redacted"; break;
  case OsmOAuth::HTTP::NotFound: r = "NotFound"; break;
  case OsmOAuth::HTTP::WrongMethod: r = "WrongMethod"; break;
  case OsmOAuth::HTTP::Conflict: r = "Conflict"; break;
  case OsmOAuth::HTTP::Gone: r = "Gone"; break;
  case OsmOAuth::HTTP::PreconditionFailed: r = "PreconditionFailed"; break;
  case OsmOAuth::HTTP::URITooLong: r = "URITooLong"; break;
  case OsmOAuth::HTTP::TooMuchData: r = "TooMuchData"; break;
  default:
    // No data from server in case of NetworkError.
    if (code.first < 0)
      return "NetworkError " + strings::to_string(code.first);
    r = "HTTP " + strings::to_string(code.first);
  }
  return r + ": " + code.second;
}

}  // namespace osm
