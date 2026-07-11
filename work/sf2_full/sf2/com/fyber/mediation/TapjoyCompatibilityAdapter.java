package com.fyber.mediation;

import com.fyber.ads.banners.mediation.BannerMediationAdapter;
import com.fyber.mediation.tapjoy.TapjoyMediationAdapter;

public final class TapjoyCompatibilityAdapter extends TapjoyMediationAdapter {
  public BannerMediationAdapter getBannerMediationAdapter() {
    return null;
  }
}
