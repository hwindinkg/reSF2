/**
 * Fyber Android SDK
 * <p>
 * Copyright (c) 2016 Fyber. All rights reserved.
 */
package com.fyber.ads.banners;

/**
 * Defines the supported sizes for mediated networks
 */
public class NetworkBannerSize {

	private final String network;
	private final BannerSize size;

	public NetworkBannerSize(String network, BannerSize size) {
		this.network = network;
		this.size = size;
	}

	/**
	 * @return the network internal name
	 */
	public String getNetwork() {
		return network;
	}

	/**
	 * @return the {@link BannerSize}
	 */
	public BannerSize getSize() {
		return size;
	}

	@Override
	public String toString() {
		return network + " " + size.toString();
	}
}
