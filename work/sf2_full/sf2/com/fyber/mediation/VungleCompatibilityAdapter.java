package com.fyber.mediation;

import com.fyber.ads.banners.mediation.BannerMediationAdapter;
import com.fyber.mediation.vungle.VungleMediationAdapter;

public final class VungleCompatibilityAdapter extends VungleMediationAdapter {
  public BannerMediationAdapter getBannerMediationAdapter() {
    return null;
  }
}
